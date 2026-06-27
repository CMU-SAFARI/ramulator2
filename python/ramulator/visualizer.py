"""Manages the Nuxt visualizer server lifecycle.

The VisualizerServer starts the Nuxt dev/production server as a subprocess,
polls until it is ready, and tears it down on exit.  It is designed to be
used either as a context manager or driven explicitly via start()/stop().

Environment variables
---------------------
RAMULATOR_VISUALIZER_PORT   Override the default port (3000).
RAMULATOR_VISUALIZER_DIR    Override the path to the visualizer/ directory.
"""

from __future__ import annotations

import logging
import os
import re
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

log = logging.getLogger("ramulator.visualizer")

_DEFAULT_PORT = 3000
_HEALTH_ENDPOINT = "/api/health"
_POLL_INTERVAL_S = 0.5
_STARTUP_TIMEOUT_S = 60
_SHUTDOWN_GRACE_S = 5

def _print_banner(port: int) -> None:
    dim = "\033[2m"
    accent = "\033[38;2;255;158;64m"
    subtle = "\033[38;2;196;168;140m"
    reset = "\033[0m"
    url = f"http://localhost:{port}"
    print()
    print(f"{dim}Ramulator2 Trace Visualizer{reset} {subtle}running at{reset} {accent}{url}{reset}")
    print(f"{subtle}Ctrl+C{reset} to Stop")
    print()


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
        raise RuntimeError(
            "Node.js is required for the visualizer but was not found on PATH.\n"
            "Install Node.js 20+ from https://nodejs.org or via your package manager."
        )
    return node


def _ensure_npx_available() -> str:
    npx = shutil.which("npx")
    if npx is None:
        raise RuntimeError("npx not found on PATH. It ships with Node.js -- is your install complete?")
    return npx


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
    """

    def __init__(
        self,
        port: int | None = None,
        dev: bool = False,
        visualizer_dir: Path | str | None = None,
    ) -> None:
        self.port = port or int(os.environ.get("RAMULATOR_VISUALIZER_PORT", _DEFAULT_PORT))
        self.dev = dev
        self.visualizer_dir = Path(visualizer_dir) if visualizer_dir else _find_visualizer_dir()
        self._proc: subprocess.Popen | None = None
        self._old_sigint = None
        self._old_sigterm = None

    def start(self) -> None:
        """Start the Nuxt server and block until it is healthy."""
        if self._proc is not None:
            raise RuntimeError("Server is already running")

        ensure_node_available()
        self._ensure_node_modules()

        env = {**os.environ, "RAMULATOR_VISUALIZER_PORT": str(self.port)}

        if self.dev:
            _ensure_npx_available()
            cmd = ["npx", "nuxt", "dev", "--port", str(self.port)]
        else:
            entry = self.visualizer_dir / ".output" / "server" / "index.mjs"
            if not entry.is_file():
                raise FileNotFoundError(
                    f"Production build not found at {entry}.\n"
                    "Run 'npm run build' inside the visualizer/ directory first."
                )
            cmd = ["node", str(entry)]
            env["PORT"] = str(self.port)
            env["NITRO_PORT"] = str(self.port)

        log.info("Starting visualizer on port %d (dev=%s) ...", self.port, self.dev)
        self._proc = subprocess.Popen(
            cmd,
            cwd=str(self.visualizer_dir),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self._install_signal_handlers()
        self._wait_until_healthy()
        _print_banner(self.port)

    def stop(self) -> None:
        """Gracefully stop the server subprocess."""
        self._restore_signal_handlers()
        proc = self._proc
        if proc is None:
            return
        self._proc = None

        if proc.poll() is not None:
            return

        log.info("Stopping visualizer (pid %d) ...", proc.pid)
        proc.terminate()
        try:
            proc.wait(timeout=_SHUTDOWN_GRACE_S)
        except subprocess.TimeoutExpired:
            log.warning("Server did not exit in %ds, sending SIGKILL", _SHUTDOWN_GRACE_S)
            proc.kill()
            proc.wait()
        log.info("Visualizer stopped.")

    def wait(self) -> None:
        """Block until the user presses Ctrl+C, then stop the server."""
        if self._proc is None:
            raise RuntimeError("Server is not running -- call start() first")
        try:
            self._proc.wait()
        except KeyboardInterrupt:
            pass
        finally:
            self.stop()

    @property
    def running(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    def __enter__(self) -> VisualizerServer:
        self.start()
        return self

    def __exit__(self, *_exc) -> None:
        self.stop()

    def _ensure_node_modules(self) -> None:
        nm = self.visualizer_dir / "node_modules"
        if nm.is_dir():
            return
        log.info("Installing visualizer dependencies (npm install) ...")
        npm = shutil.which("npm")
        if npm is None:
            raise RuntimeError("npm not found on PATH")
        subprocess.check_call(
            [npm, "install"],
            cwd=str(self.visualizer_dir),
            stdout=sys.stderr,
            stderr=subprocess.STDOUT,
        )

    def _wait_until_healthy(self) -> None:
        url = f"http://localhost:{self.port}{_HEALTH_ENDPOINT}"
        deadline = time.monotonic() + _STARTUP_TIMEOUT_S
        while time.monotonic() < deadline:
            ret = self._proc.poll()
            if ret is not None:
                stdout = self._proc.stdout.read().decode() if self._proc.stdout else ""
                raise RuntimeError(
                    f"Visualizer process exited with code {ret} during startup.\n{stdout}"
                )
            try:
                with urllib.request.urlopen(url, timeout=2) as resp:
                    if resp.status == 200:
                        return
            except (urllib.error.URLError, OSError, ConnectionError):
                pass
            time.sleep(_POLL_INTERVAL_S)
        self.stop()
        raise TimeoutError(
            f"Visualizer did not become healthy at {url} within {_STARTUP_TIMEOUT_S}s"
        )

    def _install_signal_handlers(self) -> None:
        """Ensure the server is cleaned up on SIGINT/SIGTERM."""
        def _handler(signum, frame):
            self.stop()
            sys.exit(128 + signum)

        if os.name != "nt":
            self._old_sigterm = signal.signal(signal.SIGTERM, _handler)
        self._old_sigint = signal.signal(signal.SIGINT, _handler)

    def _restore_signal_handlers(self) -> None:
        if self._old_sigint is not None:
            signal.signal(signal.SIGINT, self._old_sigint)
            self._old_sigint = None
        if self._old_sigterm is not None:
            signal.signal(signal.SIGTERM, self._old_sigterm)
            self._old_sigterm = None
