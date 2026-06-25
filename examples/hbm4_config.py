"""HBM4 example configuration and simulation script.

Companion to ``example_config.py`` (which demonstrates DDR4). The same
SimpleO3 + memory-system wiring, swapped to HBM4 with the HBM3/4
controller, HBM-aware per-bank refresh manager, and an address mapper
appropriate for HBM's PseudoChannel / Sid / BankGroup / Bank hierarchy.

Run from the repo root:

    python examples/hbm4_config.py
"""

import ramulator

# Configure the simulation frontend that sends memory requests.
frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    # HBM4 transactions are 32 bytes (BL2 over the half-rate 32-bit DQ
    # bus per pseudochannel). Match the LLC line size so the frontend
    # doesn't send 64 B requests the controller will reject.
    llc_linesize=32,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# HBM4 reference device:
#   org:    32 Gb dies in an 8-Hi stack (sid=2)  -> 32 GB stack
#   timing: 8.0 Gbps per pin (HBM4 base spec)
hbm4 = ramulator.dram.HBM4(
    org_preset="HBM4_32Gb_8Hi",
    timing_preset="HBM4_8000Mbps",
)

# Instantiate the HBM3/4 controller.
#   - HBM34PerBankRefresh: per-bank REFpb scheduling (one bank at a time,
#     not the all-bank REFab — better overlap with traffic).
#   - RoBaRaCoCh: standard row-bank-rank-column-channel mapping; works
#     for the HBM hierarchy without HBM-specific reshuffling.
ctrl = ramulator.controller.HBM34(
    dram=hbm4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

# Memory system. HBM packages contain multiple independent channels;
# CacheLineInterleave fans cache-line-sized requests across them.
mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

# Run the simulation.
sim = ramulator.Simulation(frontend, mem)
sim.run()

# sim.stats is None when running under ``ramulator export`` (config-only
# emission, no simulation). Skip the print block in that case.
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
