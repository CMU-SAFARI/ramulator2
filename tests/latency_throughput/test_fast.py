"""Fast latency-throughput: no-refresh checks and plots."""

import pytest

from tests.latency_throughput.testcases import STANDARDS
from tests.latency_throughput.utils.checks import (
    check_peak_bandwidth,
    check_streaming_peak_bandwidth,
    check_unloaded_latency,
)
from tests.latency_throughput.utils.plot import plot_lat_tp
from tests.latency_throughput.utils.sweep import extract_curves, run_sweep
from tests.utils.sim import run_streaming_only

# Sweep parameters
CI_READ_RATIOS = [100, 90, 80, 70, 60, 50]
CI_NUM_PROBES = 10000


# Cache sweep results per standard (expensive to compute)
_sweep_cache = {}
_streaming_cache = {}


def _get_sweep(std_name):
    """Run sweep once per standard, cache for reuse."""
    if std_name not in _sweep_cache:
        nops = STANDARDS[std_name]["nop_counters"]
        raw = run_sweep(std_name, nops, CI_READ_RATIOS, CI_NUM_PROBES, full=False)
        curves = extract_curves(raw, std_name)
        _sweep_cache[std_name] = curves
    return _sweep_cache[std_name]


def _get_streaming(std_name):
    """Run streaming-only once per standard, cache for reuse."""
    if std_name not in _streaming_cache:
        _streaming_cache[std_name] = run_streaming_only(std_name, full=False)
    return _streaming_cache[std_name]


@pytest.mark.latency_throughput_fast
@pytest.mark.parametrize("standard", sorted(STANDARDS.keys()))
def test_latency_throughput_fast(request, standard):
    """Run no-refresh formula checks, print % deviations, generate lat-tp plot."""
    verbose = request.config.getoption("--verbose-plot")
    curves = _get_sweep(standard)
    streaming_stats = _get_streaming(standard)

    lat_result = check_unloaded_latency(curves, standard)
    bw_result = check_peak_bandwidth(curves, standard)
    streaming_result = check_streaming_peak_bandwidth(standard, streaming_stats)

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
    print("  Max Bandwidth (probed sweep):")
    print(f"    Theoretical peak = {bw_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Measured max = {bw_result['measured_max_bw']:.1f} GB/s")
    print(f"    Deviation = {bw_result['deviation_from_theoretical_pct']:+.1f}%")
    print()
    print("  Streaming-Only Bandwidth (no probes):")
    print(f"    Theoretical peak = {streaming_result['max_theoretical_bw']:.1f} GB/s")
    print(f"    Measured = {streaming_result['measured_streaming_bw']:.1f} GB/s")
    print(f"    Deviation = {streaming_result['deviation_from_theoretical_pct']:+.1f}%")
    print(f"{'=' * 60}")

    output_dir = "tests/latency_throughput/plots/fast"
    if verbose:
        output_dir = "tests/latency_throughput/plots/fast_verbose"

    png_path = plot_lat_tp(
        curves,
        standard,
        {"latency": lat_result, "bandwidth": bw_result, "streaming": streaming_result},
        output_dir=output_dir,
        verbose=verbose,
    )
    print(f"  Plot saved: {png_path}")
