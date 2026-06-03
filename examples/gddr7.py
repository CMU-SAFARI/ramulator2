"""Example GDDR7 Ramulator2 configuration."""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import ramulator


frontend = ramulator.frontend.SimpleO3(
    clock_ratio=4,
    # traces=["./examples/traces/example_inst.trace"],
    traces=["./examples/traces/dummy_inst.trace"],
    num_expected_insts=100000,
    llc_linesize=32,
    translation=ramulator.translation.NoTranslation(max_addr=536870912),
)

gddr7 = ramulator.dram.GDDR7(
    org_preset="GDDR7_16Gb_x8_4ch",
    # timing_preset="GDDR7_TEST_28000_PAM3",
    timing_preset="GDDR7_TEST_28000",
    encoding="PAM3",  # or "NRZ"
)

ctrl = ramulator.controller.GDDR7(
    dram=gddr7,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=1,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
sim.run()

if sim.stats:
    ctrl_stats = sim.stats["memory_system"]["controller"]
    print(f"Controller cycles:     {ctrl_stats['cycles']}")
    print(f"Avg read latency:      {ctrl_stats['avg_read_latency']:.1f} cycles")
    print(f"Read requests:         {ctrl_stats['num_read_reqs']}")
    print(f"Write requests:        {ctrl_stats['num_write_reqs']}")
    print(f"Row hits:              {ctrl_stats['row_hits']}")
    print(f"Row misses:            {ctrl_stats['row_misses']}")
    print(f"Row conflicts:         {ctrl_stats['row_conflicts']}")
