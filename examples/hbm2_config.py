"""HBM2 example configuration script.

Third in the HBM example series (after ``hbm4_config.py`` and
``hbm3_loadstore_config.py``). Demonstrates HBM2 / HBM2E wiring:

  - ``HBM12`` controller (used for both HBM1 and HBM2, hence the name)
  - per-bank refresh manager scoped to the HBM PseudoChannel level
  - 32-byte transactions (HBM2 BL4 over 64-bit DQ per pseudochannel)

Run from the repo root:

    python examples/hbm2_config.py
"""

import ramulator

# SimpleO3 frontend with the standard example trace. HBM2 transactions
# are 32 bytes (BL4 * 64-bit DQ per pseudochannel / 16), so the LLC
# line size must match — leaving the default 64 B trips
# GenericDRAM::send's size validation.
frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    llc_linesize=32,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# HBM2 reference device: 8 Gb per die, 8-Hi stack -> 8 GB package.
# HBM2 (unlike HBM3/4) has no Sid level; stack height is implicit.
hbm2 = ramulator.dram.HBM2(
    org_preset="HBM2_8Gb",
    timing_preset="HBM2_2400Mbps",
)

# HBM1/2 share the same controller implementation ("HBM12") — the
# pre-HBM3 dual-bus protocol family. Per-bank refresh keeps each bank
# refresh-stall to the per-bank tRFCpb budget rather than freezing
# the whole pseudochannel.
ctrl = ramulator.controller.HBM12(
    dram=hbm2,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.PerBank(),
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
