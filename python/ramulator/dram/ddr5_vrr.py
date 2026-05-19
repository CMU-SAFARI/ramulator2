import math

from ramulator.dram.ddr5 import DDR5
from ramulator.dram.spec import TimingConstraint


class DDR5_VRR(DDR5):
    name = "DDR5_VRR"

    commands = DDR5.commands + ["VRR"]

    timing_params = DDR5.timing_params + ["nVRR"]
    timing_constraints = DDR5.timing_constraints + [
        TimingConstraint(level="Bank", preceding=["VRR"], following=["ACT"], latency="nVRR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["VRR"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["VRR"], latency="nRP"),
    ]


# Inherit all DDR5 presets, adding nVRR timing
DDR5_VRR.org_presets = DDR5.org_presets
DDR5_VRR.timing_presets = {}
for _name, _timings in DDR5.timing_presets.items():
    _vrr_timings = dict(_timings)
    # nVRR ~ 70ns * RH_radius(2) * 2 = 280ns, converted to cycles
    _vrr_timings["nVRR"] = math.ceil(280_000 / _timings["tCK_ps"])
    DDR5_VRR.timing_presets[_name] = _vrr_timings
