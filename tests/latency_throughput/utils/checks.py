"""Formula-based validation checks for DRAM simulation results.

All expected values are computed from JEDEC timing parameters via
DRAMStandard.resolve() (see spec.py).  No stored baselines are used.

Each check returns a result dict with expected, measured, and % deviation
fields, suitable for printing or feeding into the annotated plot.
"""

from tests.latency_throughput.utils.spec import Spec


def check_unloaded_latency(curves: dict, spec: Spec) -> dict:
    """Compare measured unloaded latency vs theoretical.

    Theoretical unloaded latency for a random (row-miss) probe:
        (nRP + nRCD + nCL) * tCK_ns

    Measured: average probe latency at the lowest-BW point (highest NOP)
    on the 100% read curve.
    """
    measured_ns = curves[100]["lat"][0]  # lowest BW = highest NOP = first element
    expected_ns = spec.unloaded_latency_ns
    deviation_pct = (measured_ns - expected_ns) / expected_ns * 100

    return {
        "expected_ns": expected_ns,
        "measured_ns": measured_ns,
        "deviation_pct": deviation_pct,
        "nRP": spec.nRP,
        "nRCD": spec.nRCD,
        "nCL": spec.nCL,
        "tCK_ns": spec.tCK_ns,
    }


def check_peak_bandwidth(curves: dict, spec: Spec) -> dict:
    """Compare measured max BW vs theoretical peak BW (no refresh)."""
    measured_max_bw = max(curves[100]["bw"])
    deviation_pct = (
        (measured_max_bw - spec.max_theoretical_bw) / spec.max_theoretical_bw * 100
    )
    return {
        "max_theoretical_bw": spec.max_theoretical_bw,
        "measured_max_bw": measured_max_bw,
        "deviation_from_theoretical_pct": deviation_pct,
    }


def check_refresh_limited_bandwidth(curves: dict, spec: Spec) -> dict:
    """Compare measured max BW vs refresh-limited achievable BW.

    Refresh penalty per refresh event (cycles):
        nRTP + nRP + nRFC + nRCD
    The minimum unavailable time between two READs when a refresh
    intervenes: close row (nRTP + nRP), refresh (nRFC), reopen row (nRCD).

    Refresh overhead = refresh_penalty / nREFI
    Max achievable BW = max_theoretical * (1 - refresh_overhead)
    """
    measured_max_bw = max(curves[100]["bw"])
    deviation_pct = (
        (measured_max_bw - spec.max_achievable_bw) / spec.max_achievable_bw * 100
    )
    return {
        "max_theoretical_bw": spec.max_theoretical_bw,
        "refresh_penalty_cycles": spec.refresh_penalty_cycles,
        "refresh_overhead_pct": spec.refresh_overhead * 100,
        "max_achievable_bw": spec.max_achievable_bw,
        "measured_max_bw": measured_max_bw,
        "deviation_from_achievable_pct": deviation_pct,
        "nRTP": spec.nRTP,
        "nRP": spec.nRP,
        "nRFC": spec.nRFC,
        "nRCD": spec.nRCD,
        "nREFI": spec.nREFI,
    }


def check_streaming_peak_bandwidth(streaming_stats: dict, spec: Spec) -> dict:
    """Compare pure streaming BW (no probe interference) vs theoretical peak BW."""
    measured_bw = streaming_stats["memory_system"]["controller"]["total_throughput_MBps"] / 1000.0
    deviation_pct = (measured_bw - spec.max_theoretical_bw) / spec.max_theoretical_bw * 100

    return {
        "max_theoretical_bw": spec.max_theoretical_bw,
        "measured_streaming_bw": measured_bw,
        "deviation_from_theoretical_pct": deviation_pct,
    }


def check_streaming_bandwidth(streaming_stats: dict, spec: Spec) -> dict:
    """Compare pure streaming BW vs refresh-limited achievable BW."""
    measured_bw = streaming_stats["memory_system"]["controller"]["total_throughput_MBps"] / 1000.0
    deviation_pct = (measured_bw - spec.max_achievable_bw) / spec.max_achievable_bw * 100

    return {
        "max_theoretical_bw": spec.max_theoretical_bw,
        "max_achievable_bw": spec.max_achievable_bw,
        "measured_streaming_bw": measured_bw,
        "deviation_from_achievable_pct": deviation_pct,
    }
