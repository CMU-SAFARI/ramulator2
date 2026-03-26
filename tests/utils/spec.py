"""Spec resolution: compute theoretical targets from JEDEC timing parameters.

Resolves timing parameters via DRAMStandard.resolve() and derives peak BW,
unloaded latency, and refresh overhead. Used by latency-throughput checks
and curve extraction.
"""

import ramulator
from ramulator.dram.spec import DRAMStandard

from tests.latency_throughput.testcases import STANDARDS
from tests.utils.dram import create_dram


def resolve_spec(std_name):
    """Resolve JEDEC timing parameters and compute theoretical targets.

    Returns a dict with:
        org_dict, timing_dict  -- raw resolved values from DRAMStandard.resolve()
        time_unit_ns           -- ns per simulation tick (for converting sim stats)
        tCK_ns                 -- ns per CK cycle (for computing theoretical values)
        bytes_per_req          -- bytes transferred per DRAM request
        max_theoretical_bw     -- channel_width * rate / 8 / 1000 (GB/s)
        refresh_penalty_cycles -- nRTP + nRP + nRFC + nRCD (in CK, unavailable per refresh)
        refresh_overhead       -- refresh_penalty_cycles / nREFI
        max_achievable_bw      -- max_theoretical * (1 - refresh_overhead)
        unloaded_latency_ns    -- (nRP + nRCD + nCL) * tCK_ns
    """
    cfg = STANDARDS[std_name]
    std_cls = DRAMStandard._registry[cfg["dram_class"]]

    # Instantiate and resolve
    dram_obj = create_dram(cfg)
    org_dict, timing_dict = dram_obj.resolve()

    # resolve() returns timing values in CK units (pre tick_multiplier conversion).
    # Two time references:
    #   tCK_ns     = ns per CK cycle (for theoretical formulas using timing_dict values)
    #   time_unit_ns = ns per simulation tick (for converting simulation cycle counts)
    # When tick_multiplier > 1 (HBM3), to_config() divides tCK_ps and multiplies
    # all timing params by tick_multiplier. But resolve() returns the original values.
    tick_mult = getattr(std_cls, "tick_multiplier", 1)
    tCK_ps = timing_dict["tCK_ps"]
    tCK_ns = tCK_ps / 1000.0
    time_unit_ns = tCK_ns / tick_mult  # ns per simulation tick

    # Bytes per request
    channel_width = org_dict["channel_width"]
    prefetch = std_cls.internal_prefetch_size
    bytes_per_req = channel_width * prefetch // 8

    # Max theoretical BW (GB/s)
    rate = timing_dict["rate"]
    max_theoretical_bw = channel_width * rate / 8 / 1000

    # For pseudo-channel standards with 2 PCs, the effective channel BW
    # is doubled when both PCs are interleaved
    has_pseudo_channel = "PseudoChannel" in std_cls.levels
    if has_pseudo_channel:
        pc_count = org_dict.get("pseudochannel", 2)
        max_theoretical_bw *= pc_count

    # Timing parameter name resolution (standards differ)
    nRCD = timing_dict.get("nRCDRD", timing_dict.get("nRCD"))
    nRTP = timing_dict.get("nRTPL", timing_dict.get("nRTP"))
    if nRCD is None or nRTP is None:
        raise ValueError(
            f"{std_name}: timing dict missing nRCD/nRCDRD or nRTP/nRTPL. "
            f"Available keys: {sorted(timing_dict.keys())}"
        )
    nCL = timing_dict["nCL"]
    nRP = timing_dict["nRP"]
    nRFC = timing_dict["nRFC"]
    nREFI = timing_dict["nREFI"]

    # Refresh penalty: full sequence between two READs when refresh intervenes
    # close row (nRTP + nRP) + refresh (nRFC) + reopen row (nRCD)
    refresh_penalty_cycles = nRTP + nRP + nRFC + nRCD
    refresh_overhead = refresh_penalty_cycles / nREFI

    # Max achievable BW accounting for refresh
    max_achievable_bw = max_theoretical_bw * (1 - refresh_overhead)

    # Unloaded latency: random probe hits a closed row
    # PREpb (nRP) + ACT-to-CAS (nRCD) + CAS latency (nCL)
    # These are in CK units, so multiply by tCK_ns
    unloaded_latency_cycles = nRP + nRCD + nCL
    unloaded_latency_ns = unloaded_latency_cycles * tCK_ns

    return {
        "org_dict": org_dict,
        "timing_dict": timing_dict,
        "tCK_ns": tCK_ns,
        "time_unit_ns": time_unit_ns,
        "bytes_per_req": bytes_per_req,
        "channel_width": channel_width,
        "rate": rate,
        "tick_multiplier": tick_mult,
        "has_pseudo_channel": has_pseudo_channel,
        "max_theoretical_bw": max_theoretical_bw,
        "refresh_penalty_cycles": refresh_penalty_cycles,
        "refresh_overhead": refresh_overhead,
        "max_achievable_bw": max_achievable_bw,
        "unloaded_latency_cycles": unloaded_latency_cycles,
        "unloaded_latency_ns": unloaded_latency_ns,
        "nRCD": nRCD,
        "nRTP": nRTP,
        "nCL": nCL,
        "nRP": nRP,
        "nRFC": nRFC,
        "nREFI": nREFI,
    }
