"""Full latency-throughput: refresh-enabled checks and plots."""

import pytest

from tests.latency_throughput.utils.checks import (
    check_refresh_limited_throughput,
    check_streaming_throughput,
    check_unloaded_latency,
)
from tests.latency_throughput.testcases import STANDARDS
from tests.latency_throughput.utils.plot import (
    extract_curves,
    plot_curves,
    plot_curves_annotated,
)
from tests.latency_throughput.utils.runner import run_single_config_point, run_sweep
from tests.latency_throughput.utils.spec import resolve_spec

READ_RATIOS = (100, 90, 80, 70, 60, 50)
WARMUP_CYCLES = 10_000
PROBE_REQUESTS = 10_000
STREAMING_REQUESTS = 1_000_000
LATENCY_MEASURE_MODE = "random-probe"
LATENCY_SAMPLE_COUNT = PROBE_REQUESTS

_sweep_cache: dict = {}
_streaming_cache: dict = {}


def _get_curves(cfg, spec):
    cache_key = (
        cfg["name"],
        cfg["frontend_clock_ratio"],
        LATENCY_MEASURE_MODE,
        LATENCY_SAMPLE_COUNT,
    )
    if cache_key not in _sweep_cache:
        raw = run_sweep(
            cfg,
            read_ratios=READ_RATIOS,
            num_probe_requests=PROBE_REQUESTS,
            refresh_enabled=True,
            frontend_clock_ratio=cfg["frontend_clock_ratio"],
            warmup_cycles=WARMUP_CYCLES,
            latency_measure_mode=LATENCY_MEASURE_MODE,
            latency_sample_count=LATENCY_SAMPLE_COUNT,
        )
        _sweep_cache[cache_key] = extract_curves(
            raw,
            spec,
            latency_measure_mode=LATENCY_MEASURE_MODE,
        )
    return _sweep_cache[cache_key]


def _get_streaming(cfg):
    """Run refresh-enabled streaming-only test once per standard, cache for reuse."""
    cache_key = (cfg["name"], cfg["frontend_clock_ratio"])
    if cache_key not in _streaming_cache:
        _streaming_cache[cache_key] = run_single_config_point(
            cfg,
            nop_counter=1,
            read_ratio=100,
            num_probe_requests=0,
            num_streaming_requests=STREAMING_REQUESTS,
            streaming_only=True,
            refresh_enabled=True,
            frontend_clock_ratio=cfg["frontend_clock_ratio"],
            warmup_cycles=WARMUP_CYCLES,
        )
    return _streaming_cache[cache_key]


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
    throughput_result = check_refresh_limited_throughput(curves, spec)
    streaming_result = check_streaming_throughput(streaming_stats, spec)
    assert throughput_result["measured_max_throughput"] > 0
    assert streaming_result["measured_streaming_throughput"] > 0

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
    print("  Streaming-Only Throughput (with refresh):")
    print(
        "    Theoretical peak     = "
        f"{streaming_result['max_theoretical_throughput']:.1f} GB/s"
    )
    print(
        "    Achievable w/refresh = "
        f"{streaming_result['max_achievable_throughput']:.1f} GB/s"
    )
    print(
        "    Measured             = "
        f"{streaming_result['measured_streaming_throughput']:.1f} GB/s"
    )
    print(f"    Deviation            = {streaming_result['deviation_from_achievable_pct']:+.1f}%")
    print()
    print("  Mixed Workload Throughput (random probes, with refresh):")
    print(
        "    Theoretical peak     = "
        f"{throughput_result['max_theoretical_throughput']:.1f} GB/s"
    )
    print(
        "    Achievable w/refresh = "
        f"{throughput_result['max_achievable_throughput']:.1f} GB/s"
    )
    print(f"    Measured max         = {throughput_result['measured_max_throughput']:.1f} GB/s")
    print(f"    Deviation            = {throughput_result['deviation_from_achievable_pct']:+.1f}%")
    print(f"{'=' * 60}")

    if request.config.getoption("--verbose-plot"):
        png_path = plot_curves_annotated(
            curves, cfg,
            lat_result=lat_result,
            throughput_result=throughput_result,
            streaming_result=streaming_result,
            output_dir="tests/latency_throughput/plots/full_verbose",
        )
    else:
        png_path = plot_curves(curves, cfg, output_dir="tests/latency_throughput/plots/full")
    print(f"  Plot saved: {png_path}")
