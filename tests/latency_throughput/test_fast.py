"""Fast latency-throughput: no-refresh checks and plots."""

import os
import sys
import pytest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tests.latency_throughput.utils.checks import (
    check_peak_bandwidth,
    check_streaming_peak_bandwidth,
    check_unloaded_latency,
)
from tests.latency_throughput.testcases import STANDARDS
from tests.latency_throughput.utils.plot import (
    extract_curves,
    plot_curves,
    plot_curves_annotated,
)
from tests.latency_throughput.utils.runner import run_simulation, run_sweep
from tests.latency_throughput.utils.spec import resolve_spec

READ_RATIOS = (100, 90, 80, 70, 60, 50)
WARMUP_CYCLES = 10_000
FRONTEND_CLOCK_RATIO = 4
PROBE_REQUESTS = 10_000
STREAMING_REQUESTS = 50_000

# Cache sweep + streaming results per standard (expensive to compute)
_sweep_cache: dict = {}
_streaming_cache: dict = {}


def _get_curves(cfg, spec):
    if cfg["name"] not in _sweep_cache:
        raw = run_sweep(
            cfg,
            read_ratios=READ_RATIOS,
            num_probe_requests=PROBE_REQUESTS,
            refresh_enabled=False,
            frontend_clock_ratio=FRONTEND_CLOCK_RATIO,
            warmup_cycles=WARMUP_CYCLES,
        )
        _sweep_cache[cfg["name"]] = extract_curves(raw, spec)
    return _sweep_cache[cfg["name"]]


def _get_streaming(cfg):
    if cfg["name"] not in _streaming_cache:
        _streaming_cache[cfg["name"]] = run_simulation(
            cfg,
            nop_counter=1,
            read_ratio=100,
            num_probe_requests=0,
            num_streaming_requests=STREAMING_REQUESTS,
            streaming_only=True,
            refresh_enabled=False,
            frontend_clock_ratio=FRONTEND_CLOCK_RATIO,
            warmup_cycles=WARMUP_CYCLES,
        )
    return _streaming_cache[cfg["name"]]


@pytest.mark.latency_throughput_fast
@pytest.mark.parametrize("standard", sorted(STANDARDS.keys()))
def test_latency_throughput_fast(request, standard):
    """Run no-refresh formula checks, print % deviations, generate lat-tp plot."""
    cfg = STANDARDS[standard]
    spec = resolve_spec(cfg)

    curves = _get_curves(cfg, spec)
    streaming_stats = _get_streaming(cfg)

    lat_result = check_unloaded_latency(curves, spec)
    bw_result = check_peak_bandwidth(curves, spec)
    streaming_result = check_streaming_peak_bandwidth(streaming_stats, spec)

    print(f"\n{'=' * 60}")
    print(f"  {standard} Fast Latency-Throughput Results")
    print(f"{'=' * 60}")
    print("  Unloaded Latency:")
    print(
        f"    Expected (nRP+nRCD+nCL)*tCK = "
        f"({lat_result['nRP']}+{lat_result['nRCD']}+{lat_result['nCL']})"
        f" * {lat_result['tCK_ns']:.3f} ns = "
        f"{lat_result['expected_ns']:.1f} ns"
    )
    print(f"    Measured = {lat_result['measured_ns']:.1f} ns")
    print(f"    Deviation = {lat_result['deviation_pct']:+.1f}%")
    print()
    print("  Streaming-Only Bandwidth (no probes):")
    print(f"    Theoretical peak = {streaming_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Measured = {streaming_result['measured_streaming_bw']:.1f} GB/s")
    print(f"    Deviation = {streaming_result['deviation_from_theoretical_pct']:+.1f}%")
    print()
    print("  Mixed Workload Bandwidth (random probes included):")
    print(f"    Theoretical peak = {bw_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Measured max = {bw_result['measured_max_bw']:.1f} GB/s")
    print(f"    Deviation = {bw_result['deviation_from_theoretical_pct']:+.1f}%")
    print(f"{'=' * 60}")

    # if request.config.getoption("--verbose-plot"):
    #     png_path = plot_curves_annotated(
    #         curves, cfg,
    #         lat_result=lat_result,
    #         bw_result=bw_result,
    #         streaming_result=streaming_result,
    #         output_dir="tests/latency_throughput/plots/fast_verbose",
    #     )
    # else:
    #     png_path = plot_curves(curves, cfg, output_dir="tests/latency_throughput/plots/fast")
    # Always render the annotated plot — measured / theoretical throughput
    # and unloaded latency are printed directly on the plot itself.
    # `--verbose-plot` additionally writes a copy under fast_verbose/.
    output_dir = (
        "tests/latency_throughput/plots/fast_verbose"
        if request.config.getoption("--verbose-plot")
        else "tests/latency_throughput/plots/fast"
    )
    png_path = plot_curves_annotated(
        curves, cfg,
        lat_result=lat_result,
        bw_result=bw_result,
        streaming_result=streaming_result,
        output_dir=output_dir,
    )
    print(f"  Plot saved: {png_path}")
