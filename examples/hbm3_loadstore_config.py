"""HBM3 example configuration using the LoadStoreTrace frontend.

This is the simpler counterpart to ``examples/example_config.py`` and
``examples/hbm4_config.py`` (which both use the SimpleO3 frontend with
an instruction-level trace + LLC model).

LoadStoreTrace is the right frontend when you have a memory-access
trace that's already at the LLC-miss / memory-request granularity, so
you don't need to model the CPU pipeline or last-level cache.

Trace format (one access per line, space-separated):

    LD <address>     # read
    ST <address>     # write

Address is decimal or 0x-prefixed hex. The trace replays cyclically
until the simulation hits a stop condition.

Run from the repo root:

    python examples/hbm3_loadstore_config.py
"""

import ramulator

# Memory-request trace at LLC-miss granularity. The frontend emits one
# Read / Write per line.
frontend = ramulator.frontend.LoadStoreTrace(
    clock_ratio=1,
    path="./examples/traces/example_loadstore.trace",
)

# HBM3E reference device:
#   org:    16 Gb dies in an 8-Hi stack (sid=2)  -> 16 GB stack
#   timing: 6.4 Gbps per pin (HBM3 base spec)
hbm3 = ramulator.dram.HBM3(
    org_preset="HBM3_16Gb_8hi",
    timing_preset="HBM3_6400Mbps",
)

# HBM3/4 controller with per-bank refresh.
ctrl = ramulator.controller.HBM34(
    dram=hbm3,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
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
