"""Smoke-suite simulation runner.

All component choices are visible here — no hidden defaults in shared code.
"""

from tests.smoke.testcases import STANDARDS
from tests.utils import create_dram, extract_dram_layout


def run_single(
    std_name,
    nop_counter,
    read_ratio=100,
    num_probes=10000,
    warmup=10000,
):
    """Run one simulation point and return sim.stats."""
    import ramulator

    cfg = STANDARDS[std_name]

    dram = create_dram(cfg)
    layout = extract_dram_layout(dram)

    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=cfg["frontend_clock_ratio"],
        nop_counter=nop_counter,
        num_probe_requests=num_probes,
        warmup_cycles=warmup,
        seed=12345,
        read_ratio=read_ratio,
        stream_cols=cfg["stream_cols"],
        **layout,
    )

    ctrl_cls = getattr(ramulator.controller, cfg["controller_class"])
    ctrl = ctrl_cls(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    )

    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )

    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats
