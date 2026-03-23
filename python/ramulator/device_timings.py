"""Device-timing helpers for short DRAM command-sequence tests.

See the README validation and regression test section and
``tests/device_timings/example.py`` for a canonical usage example.
"""

from dataclasses import dataclass

from ramulator._ramulator import _DeviceUnderTest as _CppDeviceUnderTest
from ramulator._validation_common import _metadata_from_dram
from ramulator._validation_common import build_addr_vec


@dataclass(frozen=True)
class ProbeResult:
    preq: str
    timing_OK: bool
    ready: bool
    row_hit: bool
    row_open: bool


class DeviceUnderTest:
    ALL = -1

    def __init__(self, dram):
        self.dram = dram
        self._cpp = _CppDeviceUnderTest(dram.to_config())

        metadata = _metadata_from_dram(dram, self._cpp)
        self.level_names = metadata["level_names"]
        self.command_names = metadata["command_names"]
        self.timings = metadata["timings"]
        self.org = metadata["org"]
        self.raw_timings = metadata["raw_timings"]
        self.tick_multiplier = metadata["tick_multiplier"]
        self.time_unit_ns = metadata["time_unit_ns"]

    def timing(self, name: str) -> int:
        return self.timings[name]

    def addr_vec(self, **levels) -> list[int]:
        return build_addr_vec(self.level_names, wildcard=self.ALL, **levels)

    def probe(self, command: str, addr_vec: list[int], clk: int) -> ProbeResult:
        return ProbeResult(**self._cpp.probe(command, addr_vec, clk))

    def issue(self, command: str, addr_vec: list[int], clk: int) -> None:
        self._cpp.issue(command, addr_vec, clk)


__all__ = [
    "ProbeResult",
    "DeviceUnderTest",
]
