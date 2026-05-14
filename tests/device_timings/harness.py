"""Device-timing helpers for short DRAM command-sequence tests.

The mental model has three layers:

1. ``probe(cmd, addr, clk) -> ProbeResult`` — read-only query. Asks the device
   "if I wanted ``cmd`` at ``clk``, what would you say?" State is unchanged.
2. ``issue(cmd, addr, clk)`` — state-mutating. Strict: the C++ side raises if
   ``cmd`` is not fully ready at ``clk``. Use it once you've confirmed legality.
3. ``assert_earliest_ready_at(cmd, addr, clk)`` — verifies a timing gate is
   tight (clk-1 still blocked, clk ready). Pair with ``issue`` when you also
   want to advance state.
"""

from dataclasses import dataclass

from ramulator._ramulator_test import _DeviceUnderTest as _CppDeviceUnderTest

from tests.validation_common import _metadata_from_dram, build_addr_vec


@dataclass(frozen=True)
class ProbeResult:
    """What the device would say about ``command`` at a given clock cycle.

    Fields:
        preq: The prerequisite command the device wants next. Equals the queried
            command if state is correct; otherwise names the command that must
            be issued first (e.g. ``"ACT"`` when probing ``"RD"`` on a closed
            bank).
        timing_OK: Whether timing alone allows the command at this cycle.
        ready: ``preq == queried_command and timing_OK`` — fully issuable now.
        row_hit: The target bank is open to the target row.
        row_open: The target bank has *some* row open.

    Two distinct "blocked" cases:
        - **state-blocked**: ``preq != queried_command`` (e.g. RD on closed
          bank → ``preq="ACT"``). ``timing_OK`` may still be ``True``.
        - **timing-blocked**: ``preq == queried_command and not timing_OK``.
    """

    preq: str
    timing_OK: bool
    ready: bool
    row_hit: bool
    row_open: bool


class DeviceUnderTest:
    """Thin Python wrapper over the C++ ``_DeviceUnderTest`` shim.

    Holds DRAM metadata (``timings``, ``level_names``, ...) and exposes
    ``probe`` / ``issue`` plus a small set of higher-level helpers.
    """

    ALL = -1

    def __init__(self, dram, channel_id: int = 0):
        self.dram = dram
        self._cpp = _CppDeviceUnderTest(dram.to_config(), channel_id=channel_id)

        metadata = _metadata_from_dram(dram, self._cpp)
        self.level_names = metadata["level_names"]
        self.command_names = metadata["command_names"]
        self.timings: dict[str, int] = metadata["timings"]
        self.org = metadata["org"]
        self.raw_timings = metadata["raw_timings"]
        self.tick_multiplier = metadata["tick_multiplier"]
        self.time_unit_ns = metadata["time_unit_ns"]

    def addr_vec(self, **levels) -> list[int]:
        """Build an addr_vec ``list[int]``.

        Missing levels default to 0; pass ``self.ALL`` (= -1) for refresh-style
        wildcard addresses (e.g. ``REFab`` targeting all banks).
        """
        return build_addr_vec(self.level_names, wildcard=self.ALL, **levels)

    def probe(self, command: str, addr_vec: list[int], clk: int) -> ProbeResult:
        """Read-only query: would ``command`` be legal at ``clk`` for ``addr_vec``?

        Returns a ``ProbeResult`` describing prerequisite, timing, ready, and
        row state. Does not mutate device state — call as many times as you
        like at any cycle.
        """
        return ProbeResult(**self._cpp.probe(command, addr_vec, clk))

    def issue(self, command: str, addr_vec: list[int], clk: int) -> None:
        """Issue ``command`` at ``clk``, mutating device state.

        Strict: the C++ side raises if the command is not fully ready (state
        and timing both correct). Pair with ``assert_earliest_ready_at`` when
        you want to verify the gate is tight before issuing.
        """
        self._cpp.issue(command, addr_vec, clk)

    def get_first_ready_clk(self, command: str, addr_vec: list[int], start: int = 0) -> int:
        """Return the first cycle at or after ``start`` where ``command`` is ready."""
        clk = start
        while not self.probe(command, addr_vec, clk=clk).ready:
            clk += 1
        return clk

    def assert_earliest_ready_at(
        self, command: str, addr_vec: list[int], clk: int
    ) -> None:
        """Assert ``command`` becomes legal at *exactly* ``clk``.

        Probes at ``clk - 1`` (if ``clk > 0``) and asserts ``not ready``; probes
        at ``clk`` and asserts ``ready``. Use this to verify a timing gate is
        tight: legal as soon as possible, not earlier.
        """
        if clk > 0:
            early = self.probe(command, addr_vec, clk=clk - 1)
            assert not early.ready, (
                f"{command} should not be ready at clk={clk - 1} "
                f"(preq={early.preq!r}, timing_OK={early.timing_OK})"
            )
        ontime = self.probe(command, addr_vec, clk=clk)
        assert ontime.ready, (
            f"{command} should be ready at clk={clk} "
            f"(preq={ontime.preq!r}, timing_OK={ontime.timing_OK})"
        )


__all__ = [
    "ProbeResult",
    "DeviceUnderTest",
]
