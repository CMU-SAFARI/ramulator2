"""
 GDDR6 DRAM module

"""

import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class GDDR6(DRAMStandard):
    """ GDDR6 DRAM module """

    name = "GDDR6"
    internal_prefetch_size = 16
    read_latency = "nCL + nBL"


    # Hierarchy
    levels = {
        "Channel" : "N_A",
        "BankGroup" : "N_A",
        "Bank" : "Closed",
        "Row" : "Closed",
        "Column" : "N_A"
    }


    # Commands
    #TODO: Add REFp2b
    commands = [
        "ACT",
        "PREab", "PREpb",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
    ]

    #Taken from Table 30 - Trueth Table Commands
    command_cycles = {"ACT": 1, "RD": 1, "RDA": 1, "WR": 1, "WRA": 1}


    # States
    states = ["Opened", "Closed", "N_A"]


    # Timing Parameters
    timing_params = [
        "rate", "nBL", "nCL", "nRCDRD", "nRCDWR",
        "nRP", "nRAS", "nRC", "nWR", "nRTP", "nCWL",
        "nCCDS", "nCCDL", "nRRDS", "nRRDL",
        "nWTRS", "nWTRL",
        "nFAW", "nRFCpb", "nRREFD",
        "nREFI", # Refresh
        "tCK_ps",
        "nRFCab",
        "nPPD"
    ]


    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }


    timing_constraints = [
        # Channel
        # Not needed. See page 90 in JEDEC JESD218. nCCDS and nCCDL are the constraints that enforce the BL timing, not nBL itself.
        # We keep it here for documentation purposes, but it doesn't actually affect the simulation since the nCCDS and nCCDL constraints are more restrictive.
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Channel
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        # Per spec, RTW = is nCL + bus turnaround and we assume nTurnaround to be 1 cycle. This parameter can be set individually if needed, but we keep it as a function of nCL for simplicity.
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + 1"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        TimingConstraint(level="Channel", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Channel", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT", "REFpb"], latency="nFAW", window=4),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["ACT", "REFpb"], latency="nFAW", window=4),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["PREab", "PREpb"], following=["PREab", "PREpb"], latency="nPPD"),

        # RAS <-> REFab
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Channel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Channel", preceding=["REFab"], following=[
            "ACT", "PREab", "PREpb", "RD", "WR", "RDA", "WRA", "REFab", "REFpb",
        ], latency="nRFCab"),

        # RAS <-> REFp2b
        #TimingConstraint(level="Channel", preceding=["ACT"], following=["REFp2b"], latency="nRRDL"),
        #TimingConstraint(level="Channel", preceding=["PREab"], following=["REFp2b"], latency="nRP"),
        #TimingConstraint(level="Channel", preceding=["RDA"], following=["REFp2b"], latency="nRP + nRTP"),
        #TimingConstraint(level="Channel", preceding=["WRA"], following=["REFp2b"], latency="nCWL + nBL + nWR + nRP"),
        #TimingConstraint(level="Channel", preceding=["REFp2b"], following=["ACT"], latency="nRREFD"),

        # RAS <-> REFpb
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["RDA"], following=["REFpb"], latency="nRP + nRTP"),
        TimingConstraint(level="Channel", preceding=["WRA"], following=["REFpb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFab"], latency="nRFCpb"),

        # Same Bank Group
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT", "REFpb"], latency="nRRDL"),

        # Bank
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA"], latency="nRCDRD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["WR", "WRA"], latency="nRCDWR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),

        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),

        # Bank RAS <-> REFpb
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["REFpb"], latency="nRP + nRTP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["REFpb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT", "REFpb"], latency="nRFCpb"),

    ]

    # Uses timings from Samsung GDDR6 8 Gb 16 banks datasheet
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK = timing_dict["tCK_ps"]

        derived_rate = round(cls.internal_prefetch_size * 1_000_000 / (timing_dict["nBL"] * tCK))
        timing_dict.setdefault("rate", derived_rate)

        timing_dict["nRRDS"] = timing_dict.get("nRRDS", max(math.ceil(4_000 / tCK), 2))
        timing_dict["nRRDL"] = timing_dict.get("nRRDL", max(math.ceil(4_000 / tCK), 2))

        timing_dict["nFAW"] = timing_dict.get("nFAW", max(math.ceil(16_000 / tCK), 8))

        timing_dict["nRFCpb"] = timing_dict.get("nRFCpb", math.ceil(60_000 / tCK))
        timing_dict["nRREFD"] = timing_dict.get("nRREFD", max(math.ceil(16_000 / tCK), 2))

        timing_dict["nREFI"] = timing_dict.get("nREFI", math.floor(1_900_000 / tCK))




GDDR6.org_presets = {
    "GDDR6_8Gb_x8": {
        "density": 8192,
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<11,
    },
    "GDDR6_8Gb_x16": {
        "density": 8192,
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<10,
    },
    "GDDR6_16Gb_x8": {
        "density": 16384,
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<15,
        "column": 1<<11,
    },
    "GDDR6_16Gb_x16": {
        "density": 16384,
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<11,
    },
    "GDDR6_32Gb_x8": {
        "density": 32768,
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<16,
        "column": 1<<11,
    },
    "GDDR6_32Gb_x16": {
        "density": 32768,
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<15,
        "column": 1<<11,
    },
}
GDDR6.timing_presets = {
    "GDDR6_14000_1350mV_double": {
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 27, "nRCDWR": 16,
        "nRP": 27, "nRAS": 53, "nRC": 79,
        "nWR": 27, "nRTP": 4, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4,
        "nRRDS": 8, "nRRDL": 8,
        "nWTRS": 9, "nWTRL": 11,
        "nFAW": 29,
        "nRFCpb": 106,
        "nRREFD": 15, "nREFI": 3333,
        "tCK_ps": 570, "nRFCab": 211, "nPPD": 1
    },
    "GDDR6_14000_1250mV_double": { # Used in Tests
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 30, "nRCDWR": 20,
        "nRP": 30, "nRAS": 60, "nRC": 90,
        "nWR": 30, "nRTP": 4, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4,
        "nRRDS": 11, "nRRDL": 11,
        "nWTRS": 9, "nWTRL": 11,
        "nFAW": 43,
        "nRFCpb": 106,
        "nRREFD": 22, "nREFI": 3333,
        "tCK_ps": 570, "nRFCab": 211, "nPPD": 2
    },
    "GDDR6_14000_1350mV_quad": {
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 27, "nRCDWR": 16,
        "nRP": 27, "nRAS": 53, "nRC": 79,
        "nWR": 27, "nRTP": 4, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4,
        "nRRDS": 8, "nRRDL": 8,
        "nWTRS": 9, "nWTRL": 11,
        "nFAW": 29,
        "nRFCpb": 106,
        "nRREFD": 15, "nREFI": 3333,
        "tCK_ps": 570, "nRFCab": 211, "nPPD": 1
    },
    "GDDR6_14000_1250mV_quad": {
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 30, "nRCDWR": 20,
        "nRP": 30, "nRAS": 60, "nRC": 90,
        "nWR": 30, "nRTP": 4, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4,
        "nRRDS": 11, "nRRDL": 11,
        "nWTRS": 9, "nWTRL": 11,
        "nFAW": 43,
        "nRFCpb": 106,
        "nRREFD": 22, "nREFI": 3333,
        "tCK_ps": 570, "nRFCab": 211, "nPPD": 2
    },
}
