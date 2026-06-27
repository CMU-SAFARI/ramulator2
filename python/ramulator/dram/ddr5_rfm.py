import math

from ramulator.dram.ddr5 import DDR5
from ramulator.dram.spec import TimingConstraint


class DDR5_RFM(DDR5):
    name = "DDR5_RFM"

    commands = DDR5.commands + ["RFMab", "RFMpb"]

    timing_params = DDR5.timing_params + ["nRFM", "nRFMpb"]
    timing_constraints = DDR5.timing_constraints + [
        # RFMab constraints — rank-scoped, same structure as REFab.
        TimingConstraint(level="Rank", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Rank", preceding=["RFMab"], following=["ACT", "PREab"], latency="nRFM"),
        # RFMpb constraints — bank-scoped, same structure as REFpb.
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["RFMpb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["RFMpb"], latency="nRP + nRTP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["RFMpb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["RFMpb"], following=["ACT", "PREpb"], latency="nRFMpb"),
    ]


# Inherit all DDR5 presets, adding nRFM timings
DDR5_RFM.org_presets = DDR5.org_presets
DDR5_RFM.timing_presets = {}
for _name, _timings in DDR5.timing_presets.items():
    _rfm_timings = dict(_timings)
    # nRFM = nRFC (same as REFab duration per JEDEC)
    _tCK_ps = _timings["tCK_ps"]
    _density = 8192  # default; will be overridden by resolve_secondary_timings
    # Use same tRFC values as DDR5
    if _density <= 8192:    _tRFC_ns = 195
    elif _density <= 16384: _tRFC_ns = 295
    else:                   _tRFC_ns = 410
    _rfm_timings["nRFM"] = math.ceil(_tRFC_ns * 1000 / _tCK_ps)
    # nRFMpb ≈ nRFCpb ≈ nRFC / 2 (per-bank refresh ~half of all-bank, per JEDEC).
    _rfm_timings["nRFMpb"] = math.ceil(_tRFC_ns * 1000 / 2 / _tCK_ps)
    DDR5_RFM.timing_presets[_name] = _rfm_timings
