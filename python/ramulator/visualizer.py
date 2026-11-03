"""Manages the Nuxt visualizer server lifecycle.

The VisualizerServer starts the Nuxt dev/production server as a subprocess,
polls until it is ready, and tears it down on exit.  It is designed to be
used either as a context manager or driven explicitly via start()/stop().
The server exits on its own if the launching process dies.

By default the server only accepts connections from this machine; from another
machine, forward the port over SSH.  Every request needs an access token, so
other users of a shared machine cannot use it either: the launcher creates one,
prints a link that carries it, and writes it to a file (readable by this user
only) that LiveTraceStreamer reads.  --no-token turns the token off; the server
then serves this machine's address only.

Environment variables
---------------------
RAMULATOR_VISUALIZER_PORT   Override the default port (3000).
RAMULATOR_VISUALIZER_DIR    Override the path to the visualizer/ directory.
RAMULATOR_VISUALIZER_TOKEN  Access token to use instead of a new one (e.g. one shared with a team).
NITRO_HOST                  Address the server listens on (default: 127.0.0.1).
"""

from __future__ import annotations

import errno
import getpass
import http.client
import ipaddress
import json
import logging
import os
import re
import secrets
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

log = logging.getLogger("ramulator.visualizer")

_DEFAULT_PORT = 3000
_HEALTH_ENDPOINT = "/api/health"
_POLL_INTERVAL_S = 0.5
_STARTUP_TIMEOUT_S = 60
_SHUTDOWN_GRACE_S = 5
_NODE_VERSIONS = "20.19+ or 22.12+"

# The server is local: never send the health check through http_proxy.
_NO_PROXY = urllib.request.build_opener(urllib.request.ProxyHandler({}))


class VisualizerError(RuntimeError):
    """The visualizer server could not be started."""


def _is_loopback(host: str) -> bool:
    if host == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def token_file(port: int) -> Path:
    """Where the launcher leaves the access token for LiveTraceStreamer on this machine."""
    cache = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(cache) / "ramulator" / f"visualizer-{port}.token"


def _ssh_hint(port: int) -> str | None:
    """How to reach a server on this machine from the computer of an SSH user."""
    ssh = os.environ.get("SSH_CONNECTION", "").split()
    if len(ssh) != 4 or os.environ.get("TERM_PROGRAM") == "vscode" or os.environ.get("CODESPACES"):
        return None  # not over SSH, or VS Code / Codespaces forward the port themselves
    server, ssh_port = ssh[2], ssh[3]
    p = "" if ssh_port == "22" else f"-p {ssh_port} "
    return f"ssh {p}-L {port}:localhost:{port} {getpass.getuser()}@{server}"


def _print_banner(url: str, traces: Path | None, ssh_hint: str | None) -> None:
    dim = "\033[2m"
    accent = "\033[38;2;255;158;64m"
    subtle = "\033[38;2;196;168;140m"
    reset = "\033[0m"
    print()
    print(f"{dim}Ramulator2 Trace Visualizer{reset} {subtle}running at{reset} {accent}{url}{reset}")
    if traces:
        print(f"{subtle}Traces from{reset} {traces}")
    if ssh_hint:
        print(f"{subtle}From your computer, run{reset} {ssh_hint}")
        print(f"{subtle}and open the link above.{reset}")
    print(f"{subtle}Ctrl+C{reset} to Stop")
    print(flush=True)


def _find_visualizer_dir() -> Path:
    """Resolve the visualizer directory next to the Python package or repo root."""
    env = os.environ.get("RAMULATOR_VISUALIZER_DIR")
    if env:
        return Path(env)

    # Walk upward from this file to find the repo root containing visualizer/
    anchor = Path(__file__).resolve().parent  # python/ramulator/
    for parent in (anchor, *anchor.parents):
        candidate = parent / "visualizer"
        if (candidate / "package.json").is_file():
            return candidate
    raise FileNotFoundError(
        "Could not locate the visualizer/ directory. "
        "Set RAMULATOR_VISUALIZER_DIR or run from the repository root."
    )


def ensure_node_available() -> str:
    """Return the absolute path to the ``node`` binary, or raise with install instructions."""
    node = shutil.which("node")
    if node is None:
        raise VisualizerError(
            "Node.js is required for the visualizer but was not found on PATH.\n"
            f"Install Node.js {_NODE_VERSIONS} from https://nodejs.org or via your package manager."
        )
    return node


def _check_node_version(node: str) -> None:
    """Warn when Node.js is older than the visualizer supports."""
    try:
        out = subprocess.run([node, "--version"], capture_output=True, text=True, timeout=10)
        version = out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return
    m = re.match(r"v(\d+)\.(\d+)", version)
    if m is None:
        return
    major, minor = int(m[1]), int(m[2])
    if (major, minor) >= (22, 12) or (major == 20 and minor >= 19):
        return
    log.warning(
        "Node.js %s is not supported; the visualizer needs Node.js %s.", version, _NODE_VERSIONS
    )


def _ensure_npx_available() -> str:
    npx = shutil.which("npx")
    if npx is None:
        raise VisualizerError(
            "npx not found on PATH. It ships with Node.js -- is your install complete?"
        )
    return npx


def _port_in_use(port: int, host: str | None) -> bool:
    """Whether another process already listens on ``port`` (on ``host``, or on any interface)."""
    try:
        addrs = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM, flags=socket.AI_PASSIVE)
    except OSError:
        return False  # an unusable host; the server reports it
    for family, kind, proto, _, addr in addrs:
        try:
            sock = socket.socket(family, kind, proto)
        except OSError:
            continue  # address family not available on this machine
        with sock:
            if os.name != "nt":
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # as Node does
            if family == socket.AF_INET6:
                sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
            try:
                sock.bind(addr)
            except OSError as e:
                if e.errno == errno.EADDRINUSE:
                    return True
    return False


def _health(url: str) -> dict | None:
    """The health response at ``url``, or None if no visualizer answers there."""
    try:
        with _NO_PROXY.open(url, timeout=2) as resp:
            if resp.status == 200:
                info = json.loads(resp.read() or b"{}")
                return info if isinstance(info, dict) else None
    except (OSError, ValueError, http.client.HTTPException):
        pass
    return None


class VisualizerServer:
    """Manage a Nuxt visualizer server subprocess.

    Parameters
    ----------
    port : int
        TCP port for the Nuxt server (default from env or 3000).
    dev : bool
        If True, run ``npx nuxt dev``.  If False, run the built production
        server (``node .output/server/index.mjs``).
    visualizer_dir : Path | str | None
        Path to the ``visualizer/`` directory.  Auto-detected if omitted.
    host : str | None
        Address to listen on (default ``NITRO_HOST`` or 127.0.0.1).
    traces : Path | str | None
        Directory whose ``.ram2bin`` files the start page lists.
    token : bool
        Require the access token (default).  Without it, the server only serves
        this machine's address, to every user of the machine.
    """

    def __init__(
        self,
        port: int | None = None,
        dev: bool = False,
        visualizer_dir: Path | str | None = None,
        host: str | None = None,
        traces: Path | str | None = None,
        token: bool = True,
    ) -> None:
        self.port = port or int(os.environ.get("RAMULATOR_VISUALIZER_PORT", _DEFAULT_PORT))
        self.dev = dev
        self.visualizer_dir = Path(visualizer_dir) if visualizer_dir else _find_visualizer_dir()
        self.host = host or os.environ.get("NITRO_HOST") or "127.0.0.1"
        self.traces = Path(traces).resolve() if traces else None
        if not token and not _is_loopback(self.host):
            raise VisualizerError(
                f"Without the access token the visualizer only serves this machine, not {self.host}."
            )
        self.token = (os.environ.get("RAMULATOR_VISUALIZER_TOKEN") or secrets.token_urlsafe(24)) if token else None
        self._proc: subprocess.Popen | None = None
        self._instance = secrets.token_hex(8)
        self._old_handlers: dict[int, object] = {}
        self._token_file: Path | None = None

    @property
    def url(self) -> str:
        """Where this machine reaches the server."""
        host = "localhost" if self.host in ("0.0.0.0", "::") else self.host
        if ":" in host:
            host = f"[{host}]"
        return f"http://{host}:{self.port}"

    @property
    def link(self) -> str:
        """The address to open in a browser, with the access token if there is one."""
        if self.host in ("0.0.0.0", "::"):
            base = f"http://{socket.getfqdn()}:{self.port}"
        else:
            base = self.url
        return f"{base}/?token={self.token}" if self.token else base

    def start(self) -> None:
        """Start the Nuxt server and block until it is healthy."""
        if self._proc is not None:
            raise RuntimeError("Server is already running")

        node = ensure_node_available()
        _check_node_version(node)
        if self.traces and not self.traces.is_dir():
            raise VisualizerError(f"Trace directory {self.traces} does not exist.")

        if _port_in_use(self.port, self.host):
            if _health(self.url + _HEALTH_ENDPOINT) is not None:
                raise VisualizerError(
                    f"A visualizer is already running at {self.url}. "
                    "Use it, stop it first, or choose another port with --port."
                )
            raise VisualizerError(
                f"Port {self.port} is already in use by another program. "
                "Choose another port with --port."
            )

        # Some shells (tcsh) export HOST as the machine name; Nitro would listen on that address.
        env = {key: value for key, value in os.environ.items() if key != "HOST"}
        env.update(
            NITRO_HOST=self.host,
            RAMULATOR_VISUALIZER_PORT=str(self.port),
            RAMULATOR_VISUALIZER_PARENT_PID=str(os.getpid()),
            RAMULATOR_VISUALIZER_INSTANCE=self._instance,
            RAMULATOR_VISUALIZER_TOKEN=self.token or "",
            RAMULATOR_VISUALIZER_NO_TOKEN="" if self.token else "1",
            RAMULATOR_VISUALIZER_TRACES=str(self.traces or ""),
        )

        if self.dev:
            npx = _ensure_npx_available()
            self._ensure_node_modules()
            cmd = [npx, "nuxt", "dev", "--port", str(self.port), "--host", self.host]
        else:
            entry = self.visualizer_dir / ".output" / "server" / "index.mjs"
            if not entry.is_file():
                raise VisualizerError(
                    f"Production build not found at {entry}.\n"
                    "Build it first: run 'npm ci' and then 'npm run build' inside visualizer/."
                )
            cmd = [node, str(entry)]
            env["PORT"] = str(self.port)
            env["NITRO_PORT"] = str(self.port)

        # Its own process group, so stop() also reaches the processes npx starts.
        if os.name == "nt":
            group = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
        else:
            group = {"start_new_session": True}

        log.info("Starting visualizer on port %d (dev=%s) ...", self.port, self.dev)
        self._proc = subprocess.Popen(cmd, cwd=str(self.visualizer_dir), env=env, **group)
        try:
            self._install_signal_handlers()
            self._wait_until_healthy()
            self._write_token_file()
        except BaseException:
            self.stop()
            raise
        ssh_hint = _ssh_hint(self.port) if _is_loopback(self.host) else None
        _print_banner(self.link, self.traces, ssh_hint)

    def stop(self) -> None:
        """Gracefully stop the server subprocess."""
        self._restore_signal_handlers()
        self._remove_token_file()
        proc = self._proc
        if proc is None:
            return
        self._proc = None

        if proc.poll() is not None:
            return

        log.info("Stopping visualizer (pid %d) ...", proc.pid)
        _stop_group(proc, force=False)
        try:
            proc.wait(timeout=_SHUTDOWN_GRACE_S)
        except subprocess.TimeoutExpired:
            log.warning("Server did not exit in %ds, killing it", _SHUTDOWN_GRACE_S)
            _stop_group(proc, force=True)
            proc.wait()
        log.info("Visualizer stopped.")

    def wait(self) -> int:
        """Block until the server exits or the user presses Ctrl+C; return the exit code."""
        proc = self._proc
        if proc is None:
            raise RuntimeError("Server is not running -- call start() first")
        try:
            proc.wait()
        except KeyboardInterrupt:
            return 128 + signal.SIGINT
        finally:
            self.stop()
        code = proc.returncode or 0
        return 128 - code if code < 0 else code

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def __enter__(self) -> VisualizerServer:
        self.start()
        return self

    def __exit__(self, *_exc) -> None:
        self.stop()

    def _write_token_file(self) -> None:
        if not self.token:
            return
        path = token_file(self.port)
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(self.token + "\n")
        self._token_file = path

    def _remove_token_file(self) -> None:
        path, self._token_file = self._token_file, None
        try:
            if path and path.read_text().strip() == self.token:
                path.unlink()
        except OSError:
            pass

    def _ensure_node_modules(self) -> None:
        nm = self.visualizer_dir / "node_modules"
        if nm.is_dir():
            return
        log.info("Installing visualizer dependencies (npm ci) ...")
        npm = shutil.which("npm")
        if npm is None:
            raise VisualizerError("npm not found on PATH")
        subprocess.check_call(
            [npm, "ci"],
            cwd=str(self.visualizer_dir),
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )

    def _wait_until_healthy(self) -> None:
        url = self.url + _HEALTH_ENDPOINT
        deadline = time.monotonic() + _STARTUP_TIMEOUT_S
        while time.monotonic() < deadline:
            ret = self._proc.poll()
            if ret is not None:
                self.stop()
                how = f"was stopped by signal {-ret}" if ret < 0 else f"exited with code {ret}"
                raise VisualizerError(
                    f"The visualizer {how} during startup (see its output above)."
                )
            info = _health(url)
            if info is not None and info.get("instance") == self._instance:
                return
            time.sleep(_POLL_INTERVAL_S)
        self.stop()
        raise VisualizerError(
            f"The visualizer did not become healthy at {url} within {_STARTUP_TIMEOUT_S}s."
        )

    def _install_signal_handlers(self) -> None:
        """Ensure the server is cleaned up on SIGINT/SIGTERM/SIGHUP.

        The handler only raises: stop() runs once the stack has unwound out of any
        Popen.wait(), whose lock it needs.
        """
        def _handler(signum, frame):
            if signum == signal.SIGINT:
                raise KeyboardInterrupt
            raise SystemExit(128 + signum)

        signums = [signal.SIGINT]
        if os.name != "nt":
            signums += [signal.SIGTERM, signal.SIGHUP]
        for signum in signums:
            self._old_handlers[signum] = signal.signal(signum, _handler)

    def _restore_signal_handlers(self) -> None:
        for signum, handler in self._old_handlers.items():
            signal.signal(signum, handler if handler is not None else signal.SIG_DFL)
        self._old_handlers.clear()


def _stop_group(proc: subprocess.Popen, force: bool) -> None:
    """Stop the server and every process it started."""
    try:
        if os.name == "nt":
            subprocess.run(
                ["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True, check=False
            )
        else:
            os.killpg(proc.pid, signal.SIGKILL if force else signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        pass
