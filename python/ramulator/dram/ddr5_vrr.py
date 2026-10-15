import math

from ramulator.dram.ddr5 import DDR5
from ramulator.dram.spec import TimingConstraint


class DDR5_VRR(DDR5):
    name = "DDR5_VRR"

    commands = DDR5.commands + ["VRR"]

    timing_params = DDR5.timing_params + ["nVRR"]
    timing_constraints = DDR5.timing_constraints + [
        TimingConstraint(level="Bank", preceding=["VRR"], following=["ACT", "VRR"], latency="nVRR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["VRR"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab", "PREsb"], following=["VRR"], latency="nRP"),
        # VRR is ordered against the refresh/RFM family in both directions.
        TimingConstraint(level="Rank", preceding=["REFab"], following=["VRR"], latency="nRFC"),
        TimingConstraint(level="Rank", preceding=["RFMab"], following=["VRR"], latency="nRFM"),
        TimingConstraint(level="Bank", preceding=["REFsb"], following=["VRR"], latency="nRFCsb"),
        TimingConstraint(level="Bank", preceding=["RFMsb"], following=["VRR"], latency="nRFMsb"),
        TimingConstraint(level="Bank", preceding=["VRR"], following=["REFsb", "RFMsb"], latency="nVRR"),
        TimingConstraint(level="Rank", preceding=["VRR"], following=["REFab", "RFMab"], latency="nVRR"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        super().resolve_secondary_timings(timing_dict, org_dict)
        timing_dict["nVRR"] = cls._resolve_nVRR(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nVRR(tCK_ps):
        return math.ceil(280_000 / tCK_ps)  # Ramulator guesstimate


# Inherit all DDR5 presets; nVRR is resolved from tCK_ps.
DDR5_VRR.org_presets = DDR5.org_presets
DDR5_VRR.timing_presets = DDR5.timing_presets
