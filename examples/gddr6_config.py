"""GDDR6 example configuration script.

Graphics-DDR6 reference — the GPU-side DRAM standard used by
GeForce / Radeon discrete GPUs. Same controller flow as DDR4 /
DDR5 (GenericDDR), but the spec layer brings GDDR6's wider DQ
(x16) and higher per-pin rates.

Channel scope: GDDR6 has no Rank, so AllBank refreshes scope to
the Channel level (handled automatically by AllBank — same
behavior as for HBM).

Run from the repo root:

    python examples/gddr6_config.py
"""

import ramulator

frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    # GDDR6 double-channel mode: each tx is 32 B.
    llc_linesize=32,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# GDDR6 reference device — 8 Gb x16 die at 16 Gbps per pin
# ("GDDR6_2000_1250mV_double" — the double-channel-mode preset that
# matches modern GeForce / Radeon traffic).
gddr6 = ramulator.dram.GDDR6(
    org_preset="GDDR6_8Gb_x16",
    timing_preset="GDDR6_2000_1250mV_double",
)

ctrl = ramulator.controller.GenericDDR(
    dram=gddr6,
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
