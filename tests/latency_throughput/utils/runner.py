"""Latency-throughput simulation runners.

`run_simulation` covers both NOP-injected probe sweeps and streaming-only
runs. It is a top-level function (not a closure) so it pickles for
ProcessPoolExecutor.
"""

import time
from concurrent.futures import ProcessPoolExecutor, as_completed

from tests.utils import extract_dram_layout


def run_simulation(
    cfg: dict,
    *,
    nop_counter: int,
    read_ratio: int,
    num_probe_requests: int,
    refresh_enabled: bool,
    frontend_clock_ratio: int,
    warmup_cycles: int,
    num_streaming_requests: int = 0,
    streaming_only: bool = False,
    stream_cls: int | None = None,
) -> dict:
    """Run one latency-throughput simulation point and return sim.stats."""
    import ramulator

    dram_cls = getattr(ramulator.dram, cfg["dram_class"])
    dram = dram_cls(org_preset=cfg["org_preset"], timing_preset=cfg["timing_preset"])
    layout = extract_dram_layout(dram)
    if stream_cls is None:
        stream_cls = layout["num_cls"] if streaming_only else cfg["stream_cls"]

    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=frontend_clock_ratio,
        nop_counter=nop_counter,
        num_probe_requests=num_probe_requests,
        num_streaming_requests=num_streaming_requests,
        streaming_only=streaming_only,
        warmup_cycles=warmup_cycles,
        seed=12345,
        read_ratio=read_ratio,
        stream_cls=stream_cls,
        **layout,
    )

    refresh_manager = (
        ramulator.refresh_manager.AllBank()
        if refresh_enabled
        else ramulator.refresh_manager.NoRefresh()
    )
    ctrl_cls = getattr(ramulator.controller, cfg["controller_class"])
    ctrl = ctrl_cls(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        refresh_manager=refresh_manager,
    )

    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )

    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats


def run_sweep(
    cfg: dict,
    *,
    read_ratios: tuple[int, ...],
    num_probe_requests: int,
    refresh_enabled: bool,
    frontend_clock_ratio: int,
    warmup_cycles: int,
    max_workers: int = 4,
) -> dict:
    """Run a parallel NOP/read-ratio sweep.

    Returns: {(nop, read_ratio): stats_dict}
    """
    jobs = [(nop, rr) for rr in read_ratios for nop in cfg["nop_counters"]]
    total = len(jobs)

    t_start = time.time()
    raw_results = {}
    with ProcessPoolExecutor(max_workers=max_workers) as pool:
        futures = {
            pool.submit(
                run_simulation,
                cfg,
                nop_counter=nop,
                read_ratio=rr,
                num_probe_requests=num_probe_requests,
                refresh_enabled=refresh_enabled,
                frontend_clock_ratio=frontend_clock_ratio,
                warmup_cycles=warmup_cycles,
            ): (nop, rr)
            for nop, rr in jobs
        }
        done = 0
        for fut in as_completed(futures):
            nop, rr = futures[fut]
            raw_results[(nop, rr)] = fut.result()
            done += 1
            if done % 10 == 0 or done == total:
                print(f"  [{cfg['name']}] {done}/{total} completed")

    elapsed = time.time() - t_start
    print(f"  [{cfg['name']}] All {total} runs done in {elapsed:.1f}s")
    return raw_results
