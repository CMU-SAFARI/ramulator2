"""HBM4 + RFM Manager controller-plugin example.

Builds on examples/hbm4_config.py with the RFMManager controller
plugin (the HBM3/4 Refresh Management mitigation for RowHammer-style
activation bursts). Demonstrates two things the other HBM4 example
doesn't:

  1. Wiring a controller plugin via ramulator.controller.HBM34's
     controller_plugins= (plural) keyword.
  2. Reading the plugin's emitted stats (rfm_issued / rfm_pending_peak)
     to confirm RFM commands actually fired during the run.

Run from the repo root:

    python examples/hbm4_rfm_manager_config.py

Note: this script requires the RFMManager plugin implementation to be
available — the plugin originates from the open ramulator2 ecosystem
and is registered automatically when present in the build. Trips a
"unknown implementation 'RFMManager'" error otherwise.
"""

import ramulator

frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    llc_linesize=32,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

hbm4 = ramulator.dram.HBM4(
    org_preset="HBM4_32Gb_8Hi",
    timing_preset="HBM4_8000Mbps",
)

# Try to instantiate the optional RFMManager plugin. Skip the plugin
# wiring if it isn't built — the rest of the example still demonstrates
# the controller_plugins= keyword shape.
try:
    rfm_plugin = ramulator.controller_plugin.RFMManager(
        activation_threshold=4096,
    )
    controller_plugins = [rfm_plugin]
    rfm_available = True
except AttributeError:
    controller_plugins = []
    rfm_available = False
    print("Note: RFMManager plugin not available in this build — "
          "running without RFM mitigation.")

ctrl = ramulator.controller.HBM34(
    dram=hbm4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    controller_plugins=controller_plugins,
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
    print(f"Row hits:              {ctrl_stats['row_hits']}")
    print(f"Row misses:            {ctrl_stats['row_misses']}")
    print(f"Row conflicts:         {ctrl_stats['row_conflicts']}")
    if rfm_available and "plugins" in ctrl_stats:
        for plugin_name, pstats in ctrl_stats.get("plugins", {}).items():
            if "rfm_issued" in pstats:
                print(f"RFM issued ({plugin_name}): {pstats['rfm_issued']}")
                print(f"RFM peak pending:      {pstats.get('rfm_pending_peak', '-')}")
