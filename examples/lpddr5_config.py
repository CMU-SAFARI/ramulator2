"""LPDDR5 example configuration script.

Fourth entry in the example series (DDR4 / HBM2 / HBM3 / HBM4 already
present). Demonstrates LPDDR5's split-activate + WCK2CK protocol family
through the dedicated ``LPDDR5`` controller.

What's LPDDR5-specific:

  - ``LPDDR5`` controller — handles ACT1/ACT2 split activation with a
    per-bank ACT2 deadline and optional CAS WCK2CK synchronization.
  - ``PerBank`` refresh manager — LPDDR5 supports per-bank refresh
    (REFpb) and using it keeps refresh stalls per-bank instead of
    freezing the entire rank.
  - LLC line size matches LPDDR5-6400's 16-byte transaction
    (BL16 over 16-bit DQ).

Run from the repo root:

    python examples/lpddr5_config.py
"""

import ramulator

# SimpleO3 frontend. LPDDR5-6400 BL16 over 16-bit DQ = 16 B / transfer
# per channel; with 2 channels combined the controller exposes tx of
# 32 B (matching HBM3/4). Pick llc_linesize=32 to fit one tx.
frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    llc_linesize=32,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# LPDDR5-6400 reference device:
#   org:    8 Gb die, x16 wide
#   timing: 6.4 Gbps per pin (mainstream mobile target, ~Snapdragon 8 Gen2 era)
lpddr5 = ramulator.dram.LPDDR5(
    org_preset="LPDDR5_8Gb_x16",
    timing_preset="LPDDR5_6400",
)

# LPDDR5 controller — needs the split-activate / WCK-sync logic that
# isn't present in GenericDDR. The "LPDDR5" controller_class is the
# correct one (smoke test under tests/smoke/testcases/lpddr5.py uses
# the same).
ctrl = ramulator.controller.LPDDR5(
    dram=lpddr5,
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
    # LPDDR5-specific stats
    if "cas_issued" in ctrl_stats:
        print(f"CAS-sync issued:       {ctrl_stats['cas_issued']}")
        print(f"CAS-sync skipped:      {ctrl_stats['cas_skipped']}")
        print(f"ACT2 deadline forced:  {ctrl_stats['act2_deadline_forced']}")
        print(f"ACT2 deferred:         {ctrl_stats['act2_deferred']}")
