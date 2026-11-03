"""Lifecycle of the visualizer server started by `python -m ramulator visualize`."""

import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest
from ramulator.visualizer import VisualizerError, VisualizerServer

REPO = Path(__file__).resolve().parents[2]
BUILT = (REPO / "visualizer" / ".output" / "server" / "index.mjs").is_file()
LOCAL = urllib.request.build_opener(urllib.request.ProxyHandler({}))

needs_node = pytest.mark.skipif(shutil.which("node") is None, reason="needs Node.js")
needs_build = pytest.mark.skipif(shutil.which("node") is None or not BUILT,
                                 reason="needs Node.js and a built visualizer")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def health(port):
    try:
        with LOCAL.open(f"http://localhost:{port}/api/health", timeout=1) as resp:
            return json.loads(resp.read())
    except OSError:
        return None


@needs_node
def test_port_held_by_another_program_is_reported():
    with socket.socket() as other:
        other.bind(("127.0.0.1", 0))
        other.listen()
        server = VisualizerServer(port=other.getsockname()[1])
        with pytest.raises(VisualizerError, match="in use by another program"):
            server.start()


@needs_build
def test_starts_despite_proxy_and_host_settings_and_refuses_a_second_server(monkeypatch):
    monkeypatch.setenv("http_proxy", "http://127.0.0.1:9")  # nothing listens there
    monkeypatch.setenv("HOST", "nonexistent.invalid")  # as tcsh exports it
    port = free_port()
    with VisualizerServer(port=port) as server:
        assert server.running
        assert health(port)["status"] == "ok"
        with pytest.raises(VisualizerError, match="already running"):
            VisualizerServer(port=port).start()
    assert health(port) is None


@needs_build
@pytest.mark.skipif(os.name == "nt", reason="POSIX signals")
def test_server_exits_when_the_launcher_is_killed():
    port = free_port()
    env = {**os.environ, "PYTHONPATH": str(REPO / "python")}
    cmd = [sys.executable, "-m", "ramulator", "visualize", "--port", str(port)]
    launcher = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 30
        while health(port) is None:
            assert launcher.poll() is None and time.monotonic() < deadline
            time.sleep(0.2)
    finally:
        launcher.send_signal(signal.SIGKILL)
        launcher.wait()
    deadline = time.monotonic() + 10
    while health(port) is not None:
        assert time.monotonic() < deadline, "the server outlived its launcher"
        time.sleep(0.2)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def get(url, headers=None):
    """Status, headers and body of a GET that follows no redirects."""
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect)
    try:
        with opener.open(urllib.request.Request(url, headers=headers or {}), timeout=5) as resp:
            return resp.status, resp.headers, resp.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


def network_address():
    """This machine's address on its network, or None (no packets are sent)."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        try:
            s.connect(("192.0.2.1", 9))
            address = s.getsockname()[0]
        except OSError:
            return None
    return None if address.startswith("127.") else address


@needs_build
def test_listens_on_this_machine_only_by_default():
    address = network_address()
    if address is None:
        pytest.skip("no network address")
    port = free_port()
    with VisualizerServer(port=port):
        assert health(port)["status"] == "ok"
        with pytest.raises(OSError), socket.create_connection((address, port), timeout=2):
            pass


@needs_build
def test_this_machine_needs_the_access_token_too(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    port = free_port()
    token_file = tmp_path / "ramulator" / f"visualizer-{port}.token"
    with VisualizerServer(port=port) as server:
        assert server.token and server.link == f"http://127.0.0.1:{port}/?token={server.token}"
        assert token_file.read_text().strip() == server.token
        assert token_file.stat().st_mode & 0o777 == 0o600
        page = f"http://localhost:{port}/"
        assert get(page)[0] == 401
        assert get(page, {"Authorization": f"Bearer {server.token}"})[0] == 200
    assert not token_file.exists()


@needs_build
def test_without_the_token_only_this_machine_is_served():
    port = free_port()
    with VisualizerServer(port=port, token=False) as server:
        assert server.token is None and server.link == f"http://127.0.0.1:{port}"
        page = f"http://localhost:{port}/"
        assert get(page)[0] == 200
        assert get(page, {"Host": f"evil.example:{port}"})[0] == 403
    with pytest.raises(VisualizerError):
        VisualizerServer(port=port, host="0.0.0.0", token=False)


@needs_build
def test_other_addresses_need_the_access_token(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path))
    port = free_port()
    token_file = tmp_path / "ramulator" / f"visualizer-{port}.token"
    with VisualizerServer(port=port, host="0.0.0.0") as server:
        assert server.token and server.link.endswith(f"/?token={server.token}")
        assert token_file.read_text().strip() == server.token
        assert token_file.stat().st_mode & 0o777 == 0o600
        page = f"http://localhost:{port}/"
        assert get(page, {"Accept": "text/html"})[0] == 401
        status, headers, _ = get(f"{page}?token={server.token}")
        assert status == 302 and headers["Location"] == "/"
        cookie = headers["Set-Cookie"].split(";")[0]
        assert get(page, {"Cookie": cookie})[0] == 200
    assert not token_file.exists()


@needs_build
def test_lists_and_serves_the_trace_directory(tmp_path):
    (tmp_path / "runs" / "sub").mkdir(parents=True)
    (tmp_path / "runs" / "a.ch0.ram2bin").write_bytes(b"RAM2BIN\0a")
    (tmp_path / "runs" / "sub" / "b.ch0.ram2bin").write_bytes(b"RAM2BIN\0b")
    (tmp_path / "runs" / "notes.txt").write_text("not a trace")
    (tmp_path / "secret.ch0.ram2bin").write_bytes(b"outside")
    (tmp_path / "runs" / "link.ch0.ram2bin").symlink_to(tmp_path / "secret.ch0.ram2bin")
    port = free_port()
    with VisualizerServer(port=port, traces=tmp_path / "runs") as server:
        api = f"http://localhost:{port}/api/traces"
        auth = {"Authorization": f"Bearer {server.token}"}
        listed = json.loads(get(api, auth)[2])
        assert sorted(f["path"] for f in listed["files"]) == ["a.ch0.ram2bin", "sub/b.ch0.ram2bin"]
        assert get(f"{api}/file?path=sub/b.ch0.ram2bin", auth)[2] == b"RAM2BIN\0b"
        for path in ("../secret.ch0.ram2bin", "link.ch0.ram2bin", "notes.txt"):
            assert get(f"{api}/file?path={path}", auth)[0] == 404
