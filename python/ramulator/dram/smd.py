import math

from ramulator.dram.spec import TimingConstraint

_SUBARRAY_IMPLS = {
    "ACT": "ACTsa",
    "PREpb": "PREpbsa",
    "PREab": "PREabsa",
    "RD": "RDsa",
    "WR": "WRsa",
    "RDA": "RDAsa",
    "WRA": "WRAsa",
}

_NACK_RESEND_NS = 100
_ACT_TO_NACK_CK = 4

def make_smd(base, *, name=None):
    missing = [c for c in ("ACT", "PREpb") if c not in base.commands]
    if missing:
        raise ValueError(f"make_smd({base.name}): base standard lacks commands {missing}")
    level_names = list(base.levels)
    if "Bank" not in level_names or "Row" not in level_names or "Subarray" in level_names:
        raise ValueError(f"make_smd({base.name}): needs Bank and Row levels and no Subarray level")

    levels = {}
    for lvl, init_state in base.levels.items():
        if lvl == "Row":
            levels["Subarray"] = "Closed"
        levels[lvl] = init_state
    
    # The subarray-aware impls are only consistent when all of the standard's open-row commands switch
    open_row_commands = [c for c in ("ACT", "PREpb", "PREab", "RD", "WR", "RDA", "WRA") if c in base.commands]
    command_impls = {c: _SUBARRAY_IMPLS[c] for c in open_row_commands}

    timing_constraints = list(base.timing_constraints) + [
        # A NACKed subarray refuses re-activation for the resend interval.
        TimingConstraint(level="Subarray", preceding=["ACT_NACK"], following=["ACT"], latency="nNACK_RESEND"),
    ]

    def resolve_secondary_timings(cls, timing_dict, org_dict):
        base.resolve_secondary_timings(timing_dict, org_dict)
        timing_dict["nACTtoNACK"] = _ACT_TO_NACK_CK
        timing_dict["nNACK_RESEND"] = math.ceil(_NACK_RESEND_NS * 1000 / timing_dict["tCK_ps"])

    def __init__(self, *, org_preset, timing_preset, rows_per_subarray=None, **overrides):
        self._rows_per_subarray = rows_per_subarray
        base.__init__(self, org_preset=org_preset, timing_preset=timing_preset, **overrides)

    def resolve(self):
        org_dict, timing_dict = base.resolve(self)

        rps = self._rows_per_subarray
        if rps is not None:
            total_rows = org_dict["subarray"] * org_dict["row"]
            if rps <= 0 or total_rows % rps != 0:
                raise ValueError(
                    f"{type(self).name}: rows_per_subarray ({rps}) must divide "
                    f"the bank's total row count ({total_rows}) evenly")
            org_dict["subarray"] = total_rows // rps
            org_dict["row"] = rps

        return org_dict, timing_dict

    cls_name = name or f"{base.name}_SMD"
    smd = type(
        cls_name,
        (base,),
        {
            "__doc__": f"{base.name} with a Subarray level and the SMD ACT_NACK command (see ramulator.dram.smd).",
            "name": cls_name,
            "levels": levels,
            "commands": list(base.commands) + ["ACT_NACK"],
            "command_impls": {**base.command_impls, **command_impls},
            "command_headers": list(base.command_headers) + ["subarray"],
            "timing_params": list(base.timing_params) + ["nACTtoNACK", "nNACK_RESEND"],
            "timing_constraints": timing_constraints,
            "resolve_secondary_timings": classmethod(resolve_secondary_timings),
            "__init__": __init__,
            "resolve": resolve,
        },
    )
    smd.org_presets = {preset: {**org, "subarray": 1} for preset, org in base.org_presets.items()}
    smd.timing_presets = base.timing_presets
    return smd
