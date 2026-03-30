"""Tier 1: Smoke tests — verify each DRAM standard runs without crashing."""

import pytest

from tests.smoke.runner import run_single
from tests.smoke.testcases import STANDARDS


@pytest.mark.smoke
@pytest.mark.parametrize("standard", sorted(STANDARDS.keys()))
def test_smoke(standard):
    """Run a short simulation (100 probes) and verify basic stats are sane."""
    stats = run_single(standard, nop_counter=5, num_probes=100, warmup=100)

    assert "memory_system" in stats
    assert "frontend" in stats
    # Verify the controller has run
    ctrl_stats = stats["memory_system"]["controller"]
    assert ctrl_stats["num_read_reqs"] > 0
    assert stats["frontend"]["avg_probe_latency"] > 0
