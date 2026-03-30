"""Latency-throughput suite simulation runners.

All component choices are visible here — no hidden defaults in shared code.
run_single is a top-level function (not a closure) so it can be pickled
for ProcessPoolExecutor.
"""

from tests.latency_throughput.testcases import STANDARDS
from tests.utils import create_dram, extract_dram_layout


def _build_controller(cfg, dram, *, full=False):
    """Instantiate the configured controller for fast or full validation."""
    import ramulator

    ctrl_cls = getattr(ramulator.controller, cfg["controller_class"])
    ctrl_kwargs = dict(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
    )
    extra_key = "full_ctrl_extra_kwargs" if full else "fast_ctrl_extra_kwargs"
    ctrl_kwargs.update(cfg[extra_key])
    return ctrl_cls(**ctrl_kwargs)


def run_single(
    std_name,
    nop_counter,
    read_ratio=100,
    num_probes=10000,
    warmup=10000,
    clock_ratio=None,
    stream_cols=None,
    full=False,
):
    """Run one simulation point and return sim.stats.

    This is a top-level function (not a closure) so it can be pickled
    for ProcessPoolExecutor.  It re-imports ramulator in each subprocess.
    """
    import ramulator

    cfg = STANDARDS[std_name]
    if clock_ratio is None:
        clock_ratio = cfg["frontend_clock_ratio"]
    if stream_cols is None:
        stream_cols = cfg["stream_cols"]

    dram = create_dram(cfg)
    layout = extract_dram_layout(dram)

    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=clock_ratio,
        nop_counter=nop_counter,
        num_probe_requests=num_probes,
        warmup_cycles=warmup,
        seed=12345,
        read_ratio=read_ratio,
        stream_cols=stream_cols,
        **layout,
    )

    ctrl = _build_controller(cfg, dram, full=full)

    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )

    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats


def run_streaming_only(std_name, num_requests=50000, full=False):
    """Run a streaming-only simulation (no probes) at maximum throughput.

    Fires sequential read requests as fast as the memory system can accept
    them, with no probe interference or NOP rate-limiting.

    Returns the stats dict from the completed simulation.
    """
    import ramulator

    cfg = STANDARDS[std_name]
    clock_ratio = cfg["frontend_clock_ratio"]
    stream_cols = cfg["stream_cols"]

    dram = create_dram(cfg)
    layout = extract_dram_layout(dram)

    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=clock_ratio,
        nop_counter=1,
        num_probe_requests=0,
        streaming_only=True,
        num_streaming_requests=num_requests,
        read_ratio=100,
        stream_cols=stream_cols,
        **layout,
    )

    ctrl = _build_controller(cfg, dram, full=full)

    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )

    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats
