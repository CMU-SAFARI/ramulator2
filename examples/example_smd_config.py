"""Example: Self-Managing DRAM (SMD) simulation"""

import ramulator

frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

org, timing = "DDR4_16Gb_x8", "DDR4_3200AA"

ctrl = ramulator.controller.SMD(
    # DDR4 with a Subarray level (8192 rows each) and the ACT_NACK command
    dram=ramulator.dram.DDR4_SMD(org_preset=org, timing_preset=timing, rank=2, rows_per_subarray=8192),
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.NoRefresh(),  # DRAM refreshes it self
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    smd_ref_policy="FixedRate",  # or "VariableRefresh"
    smd_refresh_period=51_200_000,  # cycles: 32ms at 1600MHz
    smd_num_ref_machines=16,
)

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=4,
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
    print(f"ACT_NACKs:             {ctrl_stats['smd_act_nacks']} full, {ctrl_stats['smd_act_partial_nacks']} partial")
    print(f"Maintenance ops/rows:  {ctrl_stats['smd_maintenance_ops']} / {ctrl_stats['smd_rows_refreshed']}")
    print(f"Forced precharges:     {ctrl_stats['smd_forced_precharges']}")