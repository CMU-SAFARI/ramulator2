"""Full latency-throughput: long streaming runs with refresh enabled."""

import pytest

from tests.latency_throughput.testcases import STANDARDS
from tests.latency_throughput.utils.checks import check_streaming_bandwidth
from tests.latency_throughput.runner import run_streaming_only


@pytest.mark.latency_throughput_full
@pytest.mark.parametrize("standard", sorted(STANDARDS.keys()))
def test_streaming_with_refresh_full(standard):
    """Run a long refresh-enabled streaming test and print the results."""
    stats = run_streaming_only(
        standard,
        num_requests=STANDARDS[standard]["full_streaming_requests"],
        full=True,
    )

    assert "memory_system" in stats
    assert "frontend" in stats

    result = check_streaming_bandwidth(standard, stats)
    assert result["measured_streaming_bw"] > 0

    print(f"\n{'=' * 60}")
    print(f"  {standard} Full Latency-Throughput Results")
    print(f"{'=' * 60}")
    print(f"  Theoretical peak     = {result['max_theoretical_bw']:.1f} GB/s")
    print(f"  Achievable w/refresh = {result['max_achievable_bw']:.1f} GB/s")
    print(f"  Measured streaming   = {result['measured_streaming_bw']:.1f} GB/s")
    print(f"  Deviation            = {result['deviation_from_achievable_pct']:+.1f}%")
    print(f"{'=' * 60}")
