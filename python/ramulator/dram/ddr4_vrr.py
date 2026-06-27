import math

from ramulator.dram.ddr4 import DDR4
from ramulator.dram.spec import TimingConstraint


class DDR4_VRR(DDR4):
    name = "DDR4_VRR"

    commands = DDR4.commands + ["VRR"]

    timing_params = DDR4.timing_params + ["nVRR"]
    timing_constraints = DDR4.timing_constraints + [
        TimingConstraint(level="Bank", preceding=["VRR"], following=["ACT"], latency="nVRR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["VRR"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["VRR"], latency="nRP"),
    ]


# Inherit all DDR4 presets, adding nVRR timing
DDR4_VRR.org_presets = DDR4.org_presets
DDR4_VRR.timing_presets = {}
for _name, _timings in DDR4.timing_presets.items():
    _vrr_timings = dict(_timings)
    # nVRR ~ 70ns * RH_radius(2) * 2 = 280ns, converted to cycles
    _vrr_timings["nVRR"] = math.ceil(280_000 / _timings["tCK_ps"])
    DDR4_VRR.timing_presets[_name] = _vrr_timings
