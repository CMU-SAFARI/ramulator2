"""gem5 integration — Ramulator2 as a gem5 stdlib memory system.

This module provides :class:`Memory`, a gem5 ``AbstractMemorySystem`` subclass
that wraps a Ramulator2 memory configuration built with the Python DSL.

Usage in a gem5 config script::

    import sys
    sys.path.insert(0, "/path/to/ramulator2/python")

    import ramulator
    from gem5.components.boards.simple_board import SimpleBoard
    from gem5.components.cachehierarchies.classic.no_cache import NoCache
    from gem5.components.processors.cpu_types import CPUTypes
    from gem5.components.processors.simple_processor import SimpleProcessor
    from gem5.isas import ISA
    from gem5.simulate.simulator import Simulator

    ddr4 = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
    ctrl = ramulator.controller.GenericDDR(
        dram=ddr4,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.AllBank(scope="Rank"),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    )
    mem_sys = ramulator.memory_system.GenericDRAM(
        clock_ratio=3,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
    )

    memory = ramulator.gem5.Memory(mem_sys, size="4GiB")
    board = SimpleBoard(
        clk_freq="3GHz",
        processor=SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=1),
        memory=memory,
        cache_hierarchy=NoCache(),
    )
    ...
"""

import json
from typing import List, Sequence, Tuple

from ramulator.components import Component


def _build_config(memory_system):
    """Serialize a ramulator memory_system Component (or raw dict) to JSON."""
    ms = memory_system.to_config() if isinstance(memory_system, Component) else memory_system
    config = {
        "frontend": {"impl": "External", "clock_ratio": 1},
        "memory_system": ms,
    }
    return json.dumps(config)


class Memory:
    """gem5 stdlib-compatible memory system backed by Ramulator2.

    Implements the ``AbstractMemorySystem`` interface expected by gem5's
    ``SimpleBoard``.  Follows the same pattern as gem5's built-in
    ``DRAMSysMem`` class.
    """

    def __new__(cls, memory_system, size="4GiB"):
        # We must inherit from gem5's SubSystem at class-definition time,
        # but gem5 modules are only available inside a gem5 run.  Build
        # the real class lazily on first instantiation.
        real_cls = _make_memory_class()
        instance = real_cls.__new__(real_cls)
        instance.__init__(memory_system, size)
        return instance


def _make_memory_class():
    """Construct the real AbstractMemorySystem subclass (lazy import of gem5)."""
    from m5.objects import Ramulator2
    from m5.params import AddrRange, Port
    from m5.util.convert import toMemorySize

    from gem5.components.boards.abstract_board import AbstractBoard
    from gem5.components.memory.abstract_memory_system import AbstractMemorySystem

    class _Ramulator2Memory(AbstractMemorySystem):
        def __init__(self, memory_system, size="4GiB"):
            super().__init__()
            self._size = toMemorySize(size)
            self.ramulator2 = Ramulator2(
                ramulator_config=_build_config(memory_system),
            )

        def incorporate_memory(self, board: AbstractBoard) -> None:
            pass

        def get_mem_ports(self) -> Sequence[Tuple[AddrRange, Port]]:
            return [(self.ramulator2.range, self.ramulator2.port)]

        def get_memory_controllers(self):
            return [self.ramulator2]

        def get_mem_interfaces(self):
            return [self.ramulator2]

        def get_size(self) -> int:
            return self._size

        def set_memory_range(self, ranges: List[AddrRange]) -> None:
            if len(ranges) != 1 or ranges[0].size() != self._size:
                raise Exception(
                    "Ramulator2 memory requires a single range matching "
                    f"the configured size ({self._size}).\n"
                    f"Got range size: {ranges[0].size()}"
                )
            self.ramulator2.range = ranges[0]

        def get_uninterleaved_range(self) -> List[AddrRange]:
            return [self.ramulator2.range]

    return _Ramulator2Memory
