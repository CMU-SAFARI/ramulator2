"""Controller-scheduling helpers for short request-sequence tests.

See the README validation and regression test section and
``tests/controller_scheduling/examples/`` for canonical usage examples.
"""

from dataclasses import dataclass

import ramulator
from ramulator._ramulator_test import _ControllerUnderTest as _CppControllerUnderTest
from tests.validation_common import _metadata_from_dram
from tests.validation_common import _request_type_ids
from tests.validation_common import build_addr_vec


@dataclass(frozen=True)
class IssuedCommand:
    clk: int
    command: str
    addr_vec: list[int]
    type_id: int
    source_id: int


class ControllerUnderTest:
    ALL = -1

    @classmethod
    def make_generic_ddr(
        cls,
        dram,
        *,
        scheduler=None,
        row_policy=None,
        refresh_manager=None,
        addr_mapper=None,
        controller_plugins=None,
        num_cores: int = 1,
        **kwargs,
    ):
        controller = ramulator.controller.GenericDDR(
            scheduler=scheduler or ramulator.scheduler.FRFCFS(),
            refresh_manager=refresh_manager or ramulator.refresh_manager.NoRefresh(),
            row_policy=row_policy or ramulator.row_policy.Open(),
            addr_mapper=addr_mapper or ramulator.addr_mapper.PassThroughAddrMapper(),
            dram=dram,
            controller_plugins=controller_plugins or [],
            **kwargs,
        )
        return cls(controller, num_cores=num_cores)

    @classmethod
    def make_hbm(
        cls,
        dram,
        *,
        controller_cls=None,
        scheduler=None,
        row_policy=None,
        refresh_manager=None,
        addr_mapper=None,
        controller_plugins=None,
        num_cores: int = 1,
        **kwargs,
    ):
        controller_cls = controller_cls or ramulator.controller.HBM12
        controller = controller_cls(
            scheduler=scheduler or ramulator.scheduler.FRFCFS(),
            refresh_manager=refresh_manager or ramulator.refresh_manager.NoRefresh(),
            row_policy=row_policy or ramulator.row_policy.Open(),
            addr_mapper=addr_mapper or ramulator.addr_mapper.PassThroughAddrMapper(),
            dram=dram,
            controller_plugins=controller_plugins or [],
            **kwargs,
        )
        return cls(controller, num_cores=num_cores)

    @classmethod
    def make_hbm12(cls, dram, **kwargs):
        return cls.make_hbm(dram, controller_cls=ramulator.controller.HBM12, **kwargs)

    @classmethod
    def make_gddr7(cls, dram, **kwargs):
        return cls.make_hbm(dram, controller_cls=ramulator.controller.GDDR7, **kwargs)

    @classmethod
    def make_hbm34(cls, dram, **kwargs):
        return cls.make_hbm(dram, controller_cls=ramulator.controller.HBM34, **kwargs)

    def __init__(self, controller, num_cores: int = 1):
        if not hasattr(controller, "dram"):
            raise TypeError("ControllerUnderTest requires a controller component with a dram child")

        self.controller = controller
        self.dram = controller.dram
        self._cpp = _CppControllerUnderTest(controller.to_config(), num_cores)

        metadata = _metadata_from_dram(self.dram, self._cpp)
        self.level_names = metadata["level_names"]
        self.command_names = metadata["command_names"]
        self.timings = metadata["timings"]
        self.org = metadata["org"]
        self.raw_timings = metadata["raw_timings"]
        self.tick_multiplier = metadata["tick_multiplier"]
        self.time_unit_ns = metadata["time_unit_ns"]
        self._request_type_ids = _request_type_ids(self.dram)
        self.history = []

    def timing(self, name: str) -> int:
        return self.timings[name]

    def addr_vec(self, **levels) -> list[int]:
        return build_addr_vec(self.level_names, wildcard=self.ALL, **levels)

    def send_request(self, type_name: str, addr_vec: list[int], source_id: int = 0) -> None:
        if type_name not in self._request_type_ids:
            raise ValueError(f"Unknown request type: {type_name}")
        self._cpp.send_request(self._request_type_ids[type_name], addr_vec, source_id)

    def priority_send(self, command_name: str, addr_vec: list[int]) -> None:
        if command_name not in self.command_names:
            raise ValueError(f"Unknown command: {command_name}")
        self._cpp.priority_send(command_name, addr_vec)

    def tick(self) -> list[IssuedCommand]:
        issued = [
            IssuedCommand(
                clk=item["clk"],
                command=item["command"],
                addr_vec=list(item["addr_vec"]),
                type_id=item["type_id"],
                source_id=item["source_id"],
            )
            for item in self._cpp.tick()
        ]
        self.history.extend(issued)
        return issued

    def is_idle(self) -> bool:
        return self._cpp.is_idle()

    def stats(self):
        return self._cpp.stats()

    def run_until_idle(self, max_ticks: int = 256):
        start = len(self.history)
        for _ in range(max_ticks):
            if self.is_idle():
                return self.history[start:]
            self.tick()
        if self.is_idle():
            return self.history[start:]
        raise AssertionError(f"Controller did not go idle within {max_ticks} ticks")

    def assert_commands(self, names, history=None) -> None:
        history = self.history if history is None else history
        actual = [item.command for item in history]
        expected = list(names)
        assert actual == expected, f"Expected commands {expected}, got {actual}"

    def assert_gap(self, i: int, j: int, cycles: int, history=None) -> None:
        history = self.history if history is None else history
        actual = history[j].clk - history[i].clk
        assert actual == cycles, (
            f"Expected {cycles} ticks between {history[i].command}[{i}] and "
            f"{history[j].command}[{j}], got {actual}"
        )


__all__ = [
    "IssuedCommand",
    "ControllerUnderTest",
]
