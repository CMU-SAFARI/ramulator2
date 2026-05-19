import math

from ramulator.dram.ddr5_rfm import DDR5_RFM
from ramulator.dram.spec import TimingConstraint


class DDR5_RFM_VRR(DDR5_RFM):
    """DDR5 with both RFMab (controller-visible JEDEC layer) and VRR
    (used by in-DRAM TRR plugins to model the victim refresh that happens
    inside the chip during an RFMab cycle)."""

    name = "DDR5_RFM_VRR"

    commands = DDR5_RFM.commands + ["VRR"]

    timing_params = DDR5_RFM.timing_params + ["nVRR"]
    timing_constraints = DDR5_RFM.timing_constraints + [
        TimingConstraint(level="Bank", preceding=["VRR"], following=["ACT"], latency="nVRR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["VRR"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["VRR"], latency="nRP"),
    ]


# Inherit all DDR5_RFM presets, adding nVRR timing
DDR5_RFM_VRR.org_presets = DDR5_RFM.org_presets
DDR5_RFM_VRR.timing_presets = {}
for _name, _timings in DDR5_RFM.timing_presets.items():
    _both_timings = dict(_timings)
    # nVRR ~ 70ns * RH_radius(2) * 2 = 280ns, converted to cycles (matches DDR5_VRR)
    _both_timings["nVRR"] = math.ceil(280_000 / _timings["tCK_ps"])
    DDR5_RFM_VRR.timing_presets[_name] = _both_timings
