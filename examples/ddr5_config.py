"""DDR5 example configuration script.

Fifth entry in the example series — completes coverage of the four
DRAM controller flavors v2.1 ships:

  examples/example_config.py        — DDR4 + GenericDDR
  examples/hbm2_config.py           — HBM2 + HBM12
  examples/hbm3_loadstore_config.py — HBM3 + HBM34
  examples/hbm4_config.py           — HBM4 + HBM34
  examples/lpddr5_config.py         — LPDDR5 + LPDDR5 (split-activate)
  examples/ddr5_config.py           — DDR5 + GenericDDR  (this file)

DDR5 reuses GenericDDR (same controller flow as DDR4) but the spec
layer brings new requirements:

  - ``RFM`` parameter group with ``BRC`` (bank refresh count) — feeds
    nDRFM timings; the spec throws ``ConfigurationError`` if absent.
  - Internal prefetch 16 (vs DDR4's 8) → 64-byte cache-line transactions
    so the default ``llc_linesize=64`` matches without override.

Run from the repo root:

    python examples/ddr5_config.py
"""

import ramulator

frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    # DDR5 default tx = 64 B (BL16 over x8 DQ = 16 B / device * 4 devices) —
    # matches the SimpleO3 LLC default, no override required.
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# DDR5 reference device:
#   org:    16 Gb x8 device, 2 ranks
#   timing: 4800 MT/s rev. AN (mid-range DDR5 4800)
# (v2.1 derives the DDR5 RFM timings from the spec preset directly;
# no separate BRC / RFM param group is needed.)
ddr5 = ramulator.dram.DDR5(
    org_preset="DDR5_16Gb_x8",
    timing_preset="DDR5_4800AN",
    rank=2,
)

# GenericDDR controller — same as DDR4. The DDR5 spec class wires up
# the protocol-specific timings; no controller subclass needed.
ctrl = ramulator.controller.GenericDDR(
    dram=ddr5,
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
