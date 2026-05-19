"""Full latency-throughput: refresh-enabled checks and plots."""

import pytest

from tests.latency_throughput.utils.checks import (
    check_refresh_limited_bandwidth,
    check_streaming_bandwidth,
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
STREAMING_REQUESTS = 1_000_000

_sweep_cache: dict = {}
_streaming_cache: dict = {}


def _get_curves(cfg, spec):
    if cfg["name"] not in _sweep_cache:
        raw = run_sweep(
            cfg,
            read_ratios=READ_RATIOS,
            num_probe_requests=PROBE_REQUESTS,
            refresh_enabled=True,
            frontend_clock_ratio=FRONTEND_CLOCK_RATIO,
            warmup_cycles=WARMUP_CYCLES,
        )
        _sweep_cache[cfg["name"]] = extract_curves(raw, spec)
    return _sweep_cache[cfg["name"]]


def _get_streaming(cfg):
    """Run refresh-enabled streaming-only test once per standard, cache for reuse."""
    if cfg["name"] not in _streaming_cache:
        _streaming_cache[cfg["name"]] = run_simulation(
            cfg,
            nop_counter=1,
            read_ratio=100,
            num_probe_requests=0,
            num_streaming_requests=STREAMING_REQUESTS,
            streaming_only=True,
            refresh_enabled=True,
            frontend_clock_ratio=FRONTEND_CLOCK_RATIO,
            warmup_cycles=WARMUP_CYCLES,
        )
    return _streaming_cache[cfg["name"]]


@pytest.mark.latency_throughput_full
@pytest.mark.parametrize("standard", sorted(STANDARDS.keys()))
def test_latency_throughput_full(request, standard):
    """Run refresh-enabled formula checks and generate full lat-tp plot."""
    cfg = STANDARDS[standard]
    spec = resolve_spec(cfg)

    curves = _get_curves(cfg, spec)
    streaming_stats = _get_streaming(cfg)

    assert "memory_system" in streaming_stats
    assert "frontend" in streaming_stats

    lat_result = check_unloaded_latency(curves, spec)
    bw_result = check_refresh_limited_bandwidth(curves, spec)
    streaming_result = check_streaming_bandwidth(streaming_stats, spec)
    assert bw_result["measured_max_bw"] > 0
    assert streaming_result["measured_streaming_bw"] > 0

    print(f"\n{'=' * 60}")
    print(f"  {standard} Full Latency-Throughput Results")
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
    print("  Streaming-Only Bandwidth (with refresh):")
    print(f"    Theoretical peak     = {streaming_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Achievable w/refresh = {streaming_result['max_achievable_bw']:.1f} GB/s")
    print(f"    Measured             = {streaming_result['measured_streaming_bw']:.1f} GB/s")
    print(f"    Deviation            = {streaming_result['deviation_from_achievable_pct']:+.1f}%")
    print()
    print("  Mixed Workload Bandwidth (random probes, with refresh):")
    print(f"    Theoretical peak     = {bw_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Achievable w/refresh = {bw_result['max_achievable_bw']:.1f} GB/s")
    print(f"    Measured max         = {bw_result['measured_max_bw']:.1f} GB/s")
    print(f"    Deviation            = {bw_result['deviation_from_achievable_pct']:+.1f}%")
    print(f"{'=' * 60}")

    if request.config.getoption("--verbose-plot"):
        png_path = plot_curves_annotated(
            curves, cfg,
            lat_result=lat_result,
            bw_result=bw_result,
            streaming_result=streaming_result,
            output_dir="tests/latency_throughput/plots/full_verbose",
        )
    else:
        png_path = plot_curves(curves, cfg, output_dir="tests/latency_throughput/plots/full")
    print(f"  Plot saved: {png_path}")
