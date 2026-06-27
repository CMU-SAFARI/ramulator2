"""
Simple gem5 SE-mode test using Ramulator2 as the memory system.
Tested with gem5 v25.1
"""

import sys

# Replace with the actual path
sys.path.insert(0, "/path/to/ramulator2/python")

import ramulator
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

# ── Ramulator2 memory configuration ──

ddr4 = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
ctrl = ramulator.controller.GenericDDR(
    dram=ddr4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
mem_sys = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

memory = ramulator.gem5.Memory(mem_sys, size="4GiB")

# ── gem5 system setup ──

processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=1)
cache_hierarchy = NoCache()

board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

board.set_se_binary_workload(binary=BinaryResource(local_path="/tmp/hello"))

# ── Run ──

simulator = Simulator(board=board)
simulator.run()

print("Simulation complete. Ramulator stats written to m5out/ramulator_stats.yaml")
