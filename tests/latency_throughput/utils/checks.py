"""Formula-based validation checks for DRAM simulation results.

All expected values are computed from JEDEC timing parameters via
DRAMStandard.resolve(). No stored baselines — formulas are the
single source of truth.

Each check returns a result dict with expected, measured, and % deviation.
"""

from tests.latency_throughput.utils.spec import resolve_spec


def check_unloaded_latency(curves, std_name):
    """Compare measured unloaded latency vs theoretical.

    Theoretical unloaded latency for a random (row-miss) probe:
        (nRP + nRCD + nCL) * time_unit_ns

    Measured: average probe latency at the lowest-BW point (highest NOP)
    on the 100% read curve.

    Returns dict with expected_ns, measured_ns, deviation_pct.
    """
    spec = resolve_spec(std_name)
    expected_ns = spec["unloaded_latency_ns"]

    # Use lowest-BW point on 100% read curve (first element, sorted by NOP desc)
    curve_100 = curves[100]
    measured_ns = curve_100["lat"][0]  # lowest BW = highest NOP = first element

    deviation_pct = (measured_ns - expected_ns) / expected_ns * 100

    return {
        "expected_ns": expected_ns,
        "measured_ns": measured_ns,
        "deviation_pct": deviation_pct,
        "nRP": spec["nRP"],
        "nRCD": spec["nRCD"],
        "nCL": spec["nCL"],
        "tCK_ns": spec["tCK_ns"],
    }


def check_peak_bandwidth(curves, std_name):
    """Compare measured max BW vs theoretical peak BW."""
    spec = resolve_spec(std_name)

    curve_100 = curves[100]
    measured_max_bw = max(curve_100["bw"])
    deviation_pct = (measured_max_bw - spec["max_theoretical_bw"]) / spec["max_theoretical_bw"] * 100

    return {
        "max_theoretical_bw": spec["max_theoretical_bw"],
        "measured_max_bw": measured_max_bw,
        "deviation_from_theoretical_pct": deviation_pct,
    }


def check_max_bandwidth(curves, std_name):
    """Compare measured max BW vs theoretical max achievable BW.

    Max theoretical BW = channel_width * rate / 8 / 1000 (GB/s)
    (doubled for pseudo-channel standards with 2 PCs)

    Refresh penalty per refresh event (cycles):
        nRTP + nRP + nRFC + nRCD
    This is the minimum unavailable time between two READs when a refresh
    intervenes: close row (nRTP + nRP), refresh (nRFC), reopen row (nRCD).

    Refresh overhead = refresh_penalty / nREFI
    Max achievable BW = max_theoretical * (1 - refresh_overhead)

    Measured: max BW on the 100% read curve (last element).

    Returns dict with all intermediate values and % deviation.
    """
    spec = resolve_spec(std_name)

    curve_100 = curves[100]
    measured_max_bw = max(curve_100["bw"])  # highest BW point

    deviation_pct = (measured_max_bw - spec["max_achievable_bw"]) / spec["max_achievable_bw"] * 100

    return {
        "max_theoretical_bw": spec["max_theoretical_bw"],
        "refresh_penalty_cycles": spec["refresh_penalty_cycles"],
        "refresh_overhead_pct": spec["refresh_overhead"] * 100,
        "max_achievable_bw": spec["max_achievable_bw"],
        "measured_max_bw": measured_max_bw,
        "deviation_from_achievable_pct": deviation_pct,
        "nRTP": spec["nRTP"],
        "nRP": spec["nRP"],
        "nRFC": spec["nRFC"],
        "nRCD": spec["nRCD"],
        "nREFI": spec["nREFI"],
    }


def check_streaming_peak_bandwidth(std_name, streaming_stats):
    """Compare pure streaming BW (no probe interference) vs theoretical peak BW."""
    spec = resolve_spec(std_name)
    time_unit_ns = spec["time_unit_ns"]
    bytes_per_req = spec["bytes_per_req"]

    ms = streaming_stats["memory_system"]
    fe = streaming_stats["frontend"]
    cycles = ms["controller"]["cycles"]
    streaming = fe["streaming_requests_sent"]

    measured_bw = (streaming * bytes_per_req) / (cycles * time_unit_ns)
    deviation_pct = (measured_bw - spec["max_theoretical_bw"]) / spec["max_theoretical_bw"] * 100

    return {
        "max_theoretical_bw": spec["max_theoretical_bw"],
        "measured_streaming_bw": measured_bw,
        "deviation_from_theoretical_pct": deviation_pct,
    }


def check_streaming_bandwidth(std_name, streaming_stats):
    """Compare pure streaming BW (no probe interference) vs max achievable BW.

    Runs the LatencyThroughputTrace in streaming-only mode (no probes, no NOP
    rate-limiting) and compares the achieved bandwidth against the theoretical
    max achievable BW (accounting for refresh overhead).

    Returns dict with theoretical, achievable, measured, and % deviation.
    """
    spec = resolve_spec(std_name)
    time_unit_ns = spec["time_unit_ns"]
    bytes_per_req = spec["bytes_per_req"]

    ms = streaming_stats["memory_system"]
    fe = streaming_stats["frontend"]
    cycles = ms["controller"]["cycles"]
    streaming = fe["streaming_requests_sent"]

    measured_bw = (streaming * bytes_per_req) / (cycles * time_unit_ns)
    deviation_pct = (measured_bw - spec["max_achievable_bw"]) / spec["max_achievable_bw"] * 100

    return {
        "max_theoretical_bw": spec["max_theoretical_bw"],
        "max_achievable_bw": spec["max_achievable_bw"],
        "measured_streaming_bw": measured_bw,
        "deviation_from_achievable_pct": deviation_pct,
    }
