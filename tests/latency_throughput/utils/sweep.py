"""Latency-throughput sweep and curve extraction utilities.

Provides functions for parallel sweep execution and result extraction
specific to the latency-throughput validation workflow.
"""

import time
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed

from tests.latency_throughput.runner import run_single


def run_sweep(
    std_name,
    nop_counters,
    read_ratios,
    num_probes=5000,
    warmup=10000,
    max_workers=4,
    full=False,
):
    """Run a parallel NOP x read_ratio sweep.

    Returns: {(nop, read_ratio): stats_dict}
    """
    jobs = [(nop, rr) for rr in read_ratios for nop in nop_counters]
    total = len(jobs)

    t_start = time.time()
    raw_results = {}
    with ProcessPoolExecutor(max_workers=max_workers) as pool:
        futures = {
            pool.submit(
                run_single,
                std_name,
                nop,
                read_ratio=rr,
                num_probes=num_probes,
                warmup=warmup,
                full=full,
            ): (nop, rr)
            for nop, rr in jobs
        }
        done = 0
        for fut in as_completed(futures):
            nop, rr = futures[fut]
            raw_results[(nop, rr)] = fut.result()
            done += 1
            if done % 10 == 0 or done == total:
                print(f"  [{std_name}] {done}/{total} completed")

    elapsed = time.time() - t_start
    print(f"  [{std_name}] All {total} runs done in {elapsed:.1f}s")
    return raw_results


def extract_curves(raw_results, std_name):
    """Convert raw sweep results into per-read-ratio lat-tp curves.

    Returns: {
        read_ratio: {
            "bw": [float, ...],   # GB/s, sorted low-BW first
            "lat": [float, ...],  # ns
            "nops": [int, ...],   # corresponding NOP values
        }
    }
    """
    from tests.latency_throughput.utils.spec import resolve_spec

    spec = resolve_spec(std_name)
    time_unit_ns = spec["time_unit_ns"]
    bytes_per_req = spec["bytes_per_req"]

    # Group by read ratio
    by_rr = defaultdict(list)
    for (nop, rr), stats in raw_results.items():
        by_rr[rr].append((nop, stats))

    curves = {}
    for rr, entries in sorted(by_rr.items()):
        # Sort by NOP descending (low BW first)
        entries.sort(key=lambda x: -x[0])
        bw_list, lat_list, nop_list = [], [], []
        for nop, stats in entries:
            ms = stats["memory_system"]
            fe = stats["frontend"]
            cycles = ms["controller"]["cycles"]
            streaming = fe["streaming_requests_sent"]
            lat_cycles = fe["avg_probe_latency"]

            bw = (streaming * bytes_per_req) / (cycles * time_unit_ns)
            lat = lat_cycles * time_unit_ns

            bw_list.append(bw)
            lat_list.append(lat)
            nop_list.append(nop)

        curves[rr] = {"bw": bw_list, "lat": lat_list, "nops": nop_list}

    return curves
