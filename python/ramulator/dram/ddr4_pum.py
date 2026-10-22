from ramulator.dram.ddr4 import DDR4
from ramulator.dram.spec import TimingConstraint


class DDR4_PUM(DDR4):
    """DDR4 with a Subarray level and processing-using-DRAM (PUM) commands.

    The Subarray level sits between Bank and Row. Every org preset has
    ``subarray: 1``; ``rows_per_subarray=N`` splits each bank's rows into
    subarrays of N rows without changing bank capacity.

    ==============  =========  ====================================
    command         substrate  latency
    ==============  =========  ====================================
    ``AAP``         SIMDRAM    ``nRAS + nRP + ceil(nRP/4)``
    ``AP``          SIMDRAM    ``nRAS + nRP``
    ``GB_MOV``      MIMDRAM    ``nRAS + nRELOC + nWR + nRP``
    ``LC_MOV``      MIMDRAM    ``2*(nRAS + nRP) + nRELOC + nWR``
    ``PMAJ3``       PRADA      ``nRAS + nRP + ceil(nRP/4)``
    ``PNOT``        PRADA      ``nRAS + nRP``
    ==============  =========  ====================================

    A PUM command needs its bank closed and leaves it closed. It is gated like
    an ACT, except that it is not subject to nRRDS, nRRDL or nFAW. Commands to
    the same subarray are spaced by the full latency, commands to different
    subarrays of a bank by ``nCCDM``, and the bank accepts no ACT, precharge or
    refresh until a PUM command completes.
    """

    name = "DDR4_PUM"

    levels = {
        "Channel":   "N_A",
        "Rank":      "N_A",
        "BankGroup": "N_A",
        "Bank":      "Closed",
        "Subarray":  "Closed",
        "Row":       "Closed",
        "Column":    "N_A",
    }

    commands = DDR4.commands + ["AAP", "AP", "GB_MOV", "LC_MOV", "PMAJ3", "PNOT"]

    # Open-row state lives on the subarray node, so (subarray, row) pairs do
    # not alias across subarrays.
    command_impls = {
        "ACT": "ACTsa",
        "PREpb": "PREpbsa",
        "PREab": "PREabsa",
        "RD": "RDsa",
        "WR": "WRsa",
        "RDA": "RDAsa",
        "WRA": "WRAsa",
    }
    command_headers = ["subarray"]

    # nRELOC: sense-amplifier interconnect re-point time of a MIMDRAM move.
    # nCCDM: spacing of PUM commands to different subarrays of one bank
    #   (defaults to nRRDL).
    timing_params = DDR4.timing_params + ["nRELOC", "nCCDM"]

    supported_requests = {
        "Read": "RD",
        "Write": "WR",
        "PUM_AAP": "AAP",
        "PUM_AP": "AP",
        "PUM_GB_MOV": "GB_MOV",
        "PUM_LC_MOV": "LC_MOV",
        "PUM_PMAJ3": "PMAJ3",
        "PUM_PNOT": "PNOT",
    }

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        super().resolve_secondary_timings(timing_dict, org_dict)
        timing_dict["nCCDM"] = timing_dict["nRRDL"]

    def __init__(self, *, org_preset, timing_preset, rows_per_subarray=None, **overrides):
        self._rows_per_subarray = rows_per_subarray
        super().__init__(org_preset=org_preset, timing_preset=timing_preset, **overrides)

    def resolve(self):
        org_dict, timing_dict = super().resolve()

        if "nCCDM" not in self._overrides:
            timing_dict["nCCDM"] = timing_dict["nRRDL"]

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


# ---- Timing constraints ----
_PUM = ["AAP", "AP", "GB_MOV", "LC_MOV", "PMAJ3", "PNOT"]

_AAP = "nRAS + nRP + (nRP + 3) // 4"
_AP = "nRAS + nRP"
_LATENCY = {
    "AAP": _AAP,
    "AP": _AP,
    "GB_MOV": "nRAS + nRELOC + nWR + nRP",
    "LC_MOV": "2 * (nRAS + nRP) + nRELOC + nWR",
    "PMAJ3": _AAP,
    "PNOT": _AP,
}

# Activation-rate limits PUM commands are not subject to.
_ACTIVATION_RATE_LIMITS = {"nRRDS", "nRRDL", "nFAW"}

DDR4_PUM.timing_constraints = DDR4.timing_constraints + [
    # Whatever gates an ACT gates a PUM command.
    *[TimingConstraint(level=tc.level, preceding=tc.preceding, following=_PUM,
                       latency=tc.latency, window=tc.window, sibling=tc.sibling)
      for tc in DDR4.timing_constraints
      if "ACT" in tc.following and tc.latency not in _ACTIVATION_RATE_LIMITS],

    *[TimingConstraint(level="Subarray", preceding=[cmd], following=_PUM, latency=lat)
      for cmd, lat in _LATENCY.items()],
    TimingConstraint(level="Bank", preceding=_PUM, following=_PUM, latency="nCCDM"),

    # The bank is busy until the PUM command completes.
    *[TimingConstraint(level="Bank", preceding=[cmd], following=["ACT", "PREpb"], latency=lat)
      for cmd, lat in _LATENCY.items()],
    *[TimingConstraint(level="Rank", preceding=[cmd], following=["PREab", "REFab"], latency=lat)
      for cmd, lat in _LATENCY.items()],
]


# ---- Presets ----
DDR4_PUM.org_presets = {
    name: {**org, "subarray": 1} for name, org in DDR4.org_presets.items()
}

# nRELOC = 5 follows the MIMDRAM evaluation.
DDR4_PUM.timing_presets = {
    name: {**timing, "nRELOC": 5} for name, timing in DDR4.timing_presets.items()
}
