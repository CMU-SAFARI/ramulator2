"""
GDDR7 DRAM.

CK4 timing unit.
Timing constraint values are from the JEDEC standard when available.
Otherwise are "guesstimates".
"""

import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class GDDR7(DRAMStandard):
    name = "GDDR7"
    internal_prefetch_size = 32
    read_latency = "1 + nRL + nDQERL + nBL"

    levels = {
        "Channel": "N_A",
        "Bank": "Closed",
        "Row": "Closed",
        "Column": "N_A",
    }

    commands = [
        "ACT",
        "PREpb",
        "PREab",
        "RD",
        "WR",
        "RDA",
        "WRA",
        "REFab",
        "REFpb",
        "RFMab",
        "RFMpb",
        "RCKSTRT",
        "RCKSTOP",
    ]

    command_cycles = {
        "ACT": 2,
        "RD": 2,
        "RDA": 2,
        "WR": 2,
        "WRA": 2,
        "RCKSTRT": 2,
        "RCKSTOP": 2,
        "PREpb": 1,
        "PREab": 1,
        "REFpb": 1,
        "REFab": 1,
        "RFMpb": 1,
        "RFMab": 1,
    }

    row_commands = [
        "ACT",
        "PREpb",
        "PREab",
        "REFab",
        "REFpb",
        "RFMab",
        "RFMpb",
    ]

    column_commands = [
        "RD",
        "WR",
        "RDA",
        "WRA",
        "RCKSTRT",
        "RCKSTOP",
    ]

    states = ["Opened", "Closed", "N_A"]

    timing_params = [
        "rate",
        "nBL",
        "nRL",
        "nWL",
        "nDQERL",
        "nRCDRD",
        "nRCDWR",
        "nRP",
        "nRAS",
        "nRC",
        "nRRD",
        "nRREFD",
        "nRPD",
        "nRTPSB",
        "nPPD",
        "nWR",
        "nCCD",
        "nCCDSB",
        "nWTR",
        "nWTRSB",
        "nRTW",
        "nREFI",
        "nREFIpb",
        "nRFCab",
        "nRFCpb",
        "nRDREFab",
        "nRFMab",
        "nRFMpb",
        "nRCKSTRT2RD",
        "nRD2RCKSTOP",
        "nRCKSP2ST",
        "nRCKST2SP",
        "nRCKSTOP_LAT",
        "nRCKEN",
        "nRCK_LS",
        "nRCKPST",
        "nRCK_HS",
        "tCK_ps",
    ]

    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

    timing_constraints = [
        # Column-to-column spacing, different bank.
        TimingConstraint("Channel", ["RD", "RDA"], ["RD", "RDA"], "nCCD"),
        TimingConstraint("Channel", ["WR", "WRA"], ["WR", "WRA"], "nCCD"),

        # Read/write turnaround.
        TimingConstraint("Channel", ["RD", "RDA"], ["WR", "WRA"], "nRTW"),
        TimingConstraint("Channel", ["WR", "WRA"], ["RD", "RDA"], "nWL + nBL + nWTR"),

        # Same-bank column spacing.
        TimingConstraint("Bank", ["RD", "RDA"], ["RD", "RDA"], "nCCDSB"),
        TimingConstraint("Bank", ["WR", "WRA"], ["WR", "WRA"], "nCCDSB"),
        TimingConstraint("Bank", ["WR", "WRA"], ["RD", "RDA"], "nWL + nBL + nWTRSB"),

        # Row activation/precharge.
        TimingConstraint("Channel", ["ACT"], ["ACT"], "nRRD"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["PREpb", "PREab"], "nPPD"),
        TimingConstraint("Bank", ["ACT"], ["ACT"], "nRC"),
        TimingConstraint("Bank", ["ACT"], ["RD", "RDA"], "nRCDRD"),
        TimingConstraint("Bank", ["ACT"], ["WR", "WRA"], "nRCDWR"),
        TimingConstraint("Bank", ["ACT"], ["PREpb"], "nRAS"),
        TimingConstraint("Bank", ["PREpb"], ["ACT"], "nRP"),
        TimingConstraint("Bank", ["PREpb"], ["ACT", "REFpb"], "nRPD", sibling=True),

        # Explicit precharge and auto-precharge follow-up.
        TimingConstraint("Bank", ["RD"], ["PREpb"], "nRTPSB"),
        TimingConstraint("Bank", ["WR"], ["PREpb"], "nWL + nBL + nWR"),
        TimingConstraint("Bank", ["RDA"], ["ACT", "REFpb", "RFMpb"], "nRTPSB + nRP"),
        TimingConstraint("Bank", ["WRA"], ["ACT", "REFpb", "RFMpb"], "nWL + nBL + nWR + nRP"),

        # All-bank refresh. Conservative: block normal row traffic at Channel level.
        TimingConstraint("Channel", ["ACT"], ["REFab"], "nRC"),
        TimingConstraint("Channel", ["RD", "RDA"], ["REFab"], "nRDREFab"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["REFab"], "nRP"),
        TimingConstraint("Channel", ["RDA"], ["REFab"], "nRTPSB + nRP"),
        TimingConstraint("Channel", ["WRA"], ["REFab"], "nWL + nBL + nWR + nRP"),
        TimingConstraint("Channel", ["REFab"], ["ACT", "PREab", "REFab", "REFpb"], "nRFCab"),

        # Per-bank refresh.
        TimingConstraint("Channel", ["REFpb"], ["REFpb", "ACT"], "nRREFD"),
        TimingConstraint("Channel", ["REFpb"], ["REFab"], "nRFCpb"),
        TimingConstraint("Channel", ["ACT"], ["REFpb"], "nRRD"),
        TimingConstraint("Bank", ["REFpb"], ["ACT", "REFpb"], "nRFCpb"),
        TimingConstraint("Bank", ["ACT"], ["REFpb"], "nRC"),
        TimingConstraint("Bank", ["PREpb"], ["REFpb"], "nRP"),

        # RFM command plumbing only. No RFM policy in this model.
        TimingConstraint("Channel", ["RFMab"], ["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], "nRFMab"),
        TimingConstraint("Bank", ["RFMpb"], ["ACT", "REFpb", "RFMpb"], "nRFMpb"),
        TimingConstraint("Channel", ["ACT"], ["RFMab"], "nRC"),
        TimingConstraint("Bank", ["ACT"], ["RFMpb"], "nRC"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["RFMab"], "nRP"),
        TimingConstraint("Bank", ["PREpb"], ["RFMpb"], "nRP"),

        # RCK start/stop command timing.
        TimingConstraint("Channel", ["RCKSTRT"], ["RD", "RDA"], "nRCKSTRT2RD"),
        TimingConstraint("Channel", ["RD", "RDA"], ["RCKSTOP"], "nRD2RCKSTOP"),
        TimingConstraint("Channel", ["RCKSTOP"], ["RCKSTRT", "RD", "RDA"], "nRCKSP2ST"),
        TimingConstraint("Channel", ["RCKSTRT"], ["RCKSTOP"], "nRCKST2SP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK = timing_dict["tCK_ps"]

        timing_dict["rate"] = round(cls.internal_prefetch_size * 1_000_000 / (timing_dict["nBL"] * tCK))

        # Fixed/derived GDDR7 values.
        timing_dict.setdefault("nCCD", timing_dict["nBL"])
        timing_dict.setdefault("nCCDSB", 4)
        timing_dict.setdefault("nPPD", 2)
        timing_dict.setdefault("nRPD", timing_dict["nPPD"])
        timing_dict.setdefault("nREFI", math.floor(1_900_000 / tCK))
        timing_dict.setdefault("nREFIpb", max(1, math.floor(timing_dict["nREFI"] / 16)))

        # GDDR7 formulas that depend on resolved latency fields.
        timing_dict.setdefault(
            "nRTW",
            max(1, timing_dict["nRL"] + timing_dict["nDQERL"] + timing_dict["nBL"] + 3 - timing_dict["nWL"] + 1),
        )
        timing_dict.setdefault(
            "nRDREFab",
            timing_dict["nRL"] + timing_dict["nDQERL"] + timing_dict["nBL"] + 2,
        )

        # RCK defaults.
        timing_dict.setdefault("nRCKSTRT2RD", 2)
        timing_dict.setdefault("nRCKPST", 2)
        timing_dict.setdefault("nRCKEN", 6)
        timing_dict.setdefault("nRCKSTOP_LAT", 10)
        timing_dict.setdefault("nRCK_LS", 2)
        timing_dict.setdefault(
            "nRCK_HS",
            max(0, timing_dict["nRL"] + timing_dict["nRCKSTRT2RD"] - timing_dict["nRCKEN"] - timing_dict["nRCK_LS"]),
        )
        timing_dict.setdefault(
            "nRD2RCKSTOP",
            max(
                1,
                timing_dict["nRL"]
                + timing_dict["nDQERL"]
                + timing_dict["nBL"]
                + timing_dict["nRCKPST"]
                - timing_dict["nRCKSTOP_LAT"],
            ),
        )
        # JESD239D marks tRCKSP2ST vendor-specific; use a conservative simulator floor.
        timing_dict.setdefault("nRCKSP2ST", max(8, timing_dict["nRCKEN"] + timing_dict["nRCK_LS"]))
        timing_dict.setdefault("nRCKST2SP", timing_dict["nRCKEN"] + timing_dict["nRCK_LS"] + timing_dict["nRCK_HS"])

        # RFM command-plumbing defaults. These are simulator placeholders, not JEDEC equality rules.
        timing_dict.setdefault("nRFMab", timing_dict["nRFCab"])
        timing_dict.setdefault("nRFMpb", timing_dict["nRFCpb"])

        for name in cls.timing_params:
            if name not in timing_dict:
                raise ValueError(f"GDDR7: timing {name} remains unresolved")


GDDR7.org_presets = {
    "GDDR7_16Gb_x8": {
        "density": 4096,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 14,
        "column": (1 << 6) << 5,
    },
    "GDDR7_32Gb_x8": {
        "density": 8192,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 15,
        "column": (1 << 6) << 5,
    },
    "GDDR7_64Gb_x8": {
        "density": 16384,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 16,
        "column": (1 << 6) << 5,
    },
}


GDDR7.timing_presets = {
    "GDDR7_28000_PAM3": {
        # CI/smoke/regression only. This is not a vendor timing table.
        "nBL": 2,
        "nRL": 24,
        "nWL": 6,
        "nDQERL": 0,
        "nRCDRD": 30,
        "nRCDWR": 19,
        "nRP": 30,
        "nRAS": 60,
        "nRC": 90,
        "nWR": 30,
        "nRTPSB": 4,
        "nRRD": 4,
        "nRREFD": 21,
        "nWTR": 9,
        "nWTRSB": 11,
        "nRTW": 12,
        "nRFCab": 315,
        "nRFCpb": 105,
        "tCK_ps": 571,
    },
}
