"""DDR3 example configuration script.

Legacy DDR3 reference — DDR3 + GenericDDR is the simplest config
in v2.1 (no rank scope arg, no RFM, no split-activate). Useful as
a smoke-test starting point or for studies that compare a modern
spec (DDR5 / LPDDR5 / HBM) against the DDR3 baseline.

Run from the repo root:

    python examples/ddr3_config.py
"""

import ramulator

frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    # DDR3 BL8 over x8 DQ = 64 B / rank — matches default LLC line size.
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# DDR3 reference device — DDR3-1600H, 2 Gb x8, 1 rank.
ddr3 = ramulator.dram.DDR3(
    org_preset="DDR3_2Gb_x8",
    timing_preset="DDR3_1600H",
    rank=1,
)

ctrl = ramulator.controller.GenericDDR(
    dram=ddr3,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
sim.run()

stats = sim.stats
if stats:
    ctrl_stats = stats["memory_system"]["controller"]

    print(f"Controller cycles:     {ctrl_stats['cycles']}")
    print(f"Avg read latency:      {ctrl_stats['avg_read_latency']:.1f} cycles")
    print(f"Read requests:         {ctrl_stats['num_read_reqs']}")
    print(f"Write requests:        {ctrl_stats['num_write_reqs']}")
    print(f"Row hits:              {ctrl_stats['row_hits']}")
    print(f"Row misses:            {ctrl_stats['row_misses']}")
    print(f"Row conflicts:         {ctrl_stats['row_conflicts']}")
