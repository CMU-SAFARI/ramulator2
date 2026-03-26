import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class DDR3(DRAMStandard):
    name = "DDR3"
    internal_prefetch_size = 8
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name → init state) ----
    levels = {
        "Channel":  "N_A",
        "Rank":     "N_A",
        "Bank":     "Closed",
        "Row":      "Closed",
        "Column":   "N_A",
    }

    # ---- Commands ----
    commands = ["ACT", "PREpb", "PREab", "RD", "WR", "RDA", "WRA", "REFab"]

    # ---- States ----
    states = ["Opened", "Closed", "N_A"]

    # ---- Timing parameters (C++ Timing enum order) ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCD", "nRP", "nRAS", "nRC",
        "nWR", "nRTP", "nCWL", "nCCD", "nRRD", "nWTR",
        "nFAW", "nRFC", "nREFI", "nCS", "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

    # ---- Timing constraints ----
    timing_constraints = [
        # Channel — data bus occupancy
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Rank — CAS timing
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCD"),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCD"),
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + 2 - nCWL"),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTR"),
        # Rank — sibling (rank switching)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA", "WR", "WRA"], latency="nBL + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nCS - nCL", window=1, sibling=True),
        # Rank — CAS to PREab
        TimingConstraint(level="Rank", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Rank — RAS timing (unified nRRD — no S/L split)
        TimingConstraint(level="Rank", preceding=["ACT"], following=["ACT"], latency="nRRD"),
        TimingConstraint(level="Rank", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # Rank — RAS to REF
        TimingConstraint(level="Rank", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT"], latency="nRFC"),

        # Bank — single-bank timing
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA", "WR", "WRA"], latency="nRCD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        timing_dict["nRRD"] = cls._resolve_nRRD(org_dict["dq"], timing_dict["rate"])
        timing_dict["nFAW"] = cls._resolve_nFAW(org_dict["dq"], timing_dict["rate"])
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nRRD(dq, rate):
        if dq <= 8:  # 1KB page (x4/x8)
            if rate <= 1333: return 4
            if rate <= 1866: return 5
            if rate <= 2133: return 6
            return -1
        # 2KB page (x16)
        if rate <= 800: return 4
        if rate <= 1066: return 6
        if rate <= 1333: return 5
        if rate <= 1866: return 6
        if rate <= 2133: return 7
        return -1

    @staticmethod
    def _resolve_nFAW(dq, rate):
        if dq <= 8:  # 1KB page (x4/x8)
            if rate <= 800: return 16
            if rate <= 1333: return 20
            if rate <= 1600: return 24
            if rate <= 1866: return 26
            if rate <= 2133: return 27
            return -1
        # 2KB page (x16)
        if rate <= 800: return 20
        if rate <= 1066: return 27
        if rate <= 1333: return 30
        if rate <= 1600: return 32
        if rate <= 1866: return 33
        if rate <= 2133: return 34
        return -1

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        if density <= 1024: tRFC_ns = 110
        elif density <= 2048: tRFC_ns = 160
        elif density <= 4096: tRFC_ns = 260
        elif density <= 8192: tRFC_ns = 350
        else: return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return math.ceil(7_800_000 / tCK_ps)


# ---- DDR3 JEDEC preset data ----

DDR3.org_presets = {
    "DDR3_1Gb_x4":  {"density": 1024, "dq": 4,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<14, "column": 1<<11},
    "DDR3_1Gb_x8":  {"density": 1024, "dq": 8,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<14, "column": 1<<10},
    "DDR3_1Gb_x16": {"density": 1024, "dq": 16, "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<13, "column": 1<<10},
    "DDR3_2Gb_x4":  {"density": 2048, "dq": 4,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<15, "column": 1<<11},
    "DDR3_2Gb_x8":  {"density": 2048, "dq": 8,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<15, "column": 1<<10},
    "DDR3_2Gb_x16": {"density": 2048, "dq": 16, "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<14, "column": 1<<10},
    "DDR3_4Gb_x4":  {"density": 4096, "dq": 4,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<16, "column": 1<<11},
    "DDR3_4Gb_x8":  {"density": 4096, "dq": 8,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<16, "column": 1<<10},
    "DDR3_4Gb_x16": {"density": 4096, "dq": 16, "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<15, "column": 1<<10},
    "DDR3_8Gb_x4":  {"density": 8192, "dq": 4,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<17, "column": 1<<11},
    "DDR3_8Gb_x8":  {"density": 8192, "dq": 8,  "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<17, "column": 1<<10},
    "DDR3_8Gb_x16": {"density": 8192, "dq": 16, "channel_width": 64, "rank": 1, "bank": 8, "row": 1<<16, "column": 1<<10},
}

# Primary timings only — secondary timings (nRRD, nFAW, nRFC, nREFI)
# are resolved from JEDEC tables in resolve_secondary_timings().
DDR3.timing_presets = {
    "DDR3_800D":  {"rate": 800,  "nBL": 4, "nCL": 5,  "nRCD": 5,  "nRP": 5,  "nRAS": 15, "nRC": 20, "nWR": 6,  "nRTP": 4, "nCWL": 5,  "nCCD": 4, "nWTR": 4, "nCS": 2, "tCK_ps": 2500},
    "DDR3_800E":  {"rate": 800,  "nBL": 4, "nCL": 6,  "nRCD": 6,  "nRP": 6,  "nRAS": 15, "nRC": 21, "nWR": 6,  "nRTP": 4, "nCWL": 5,  "nCCD": 4, "nWTR": 4, "nCS": 2, "tCK_ps": 2500},
    "DDR3_1066E": {"rate": 1066, "nBL": 4, "nCL": 6,  "nRCD": 6,  "nRP": 6,  "nRAS": 20, "nRC": 26, "nWR": 8,  "nRTP": 4, "nCWL": 6,  "nCCD": 4, "nWTR": 4, "nCS": 2, "tCK_ps": 1875},
    "DDR3_1066F": {"rate": 1066, "nBL": 4, "nCL": 7,  "nRCD": 7,  "nRP": 7,  "nRAS": 20, "nRC": 27, "nWR": 8,  "nRTP": 4, "nCWL": 6,  "nCCD": 4, "nWTR": 4, "nCS": 2, "tCK_ps": 1875},
    "DDR3_1066G": {"rate": 1066, "nBL": 4, "nCL": 8,  "nRCD": 8,  "nRP": 8,  "nRAS": 20, "nRC": 28, "nWR": 8,  "nRTP": 4, "nCWL": 6,  "nCCD": 4, "nWTR": 4, "nCS": 2, "tCK_ps": 1875},
    "DDR3_1333G": {"rate": 1333, "nBL": 4, "nCL": 8,  "nRCD": 8,  "nRP": 8,  "nRAS": 24, "nRC": 32, "nWR": 10, "nRTP": 5, "nCWL": 7,  "nCCD": 4, "nWTR": 5, "nCS": 2, "tCK_ps": 1500},
    "DDR3_1333H": {"rate": 1333, "nBL": 4, "nCL": 9,  "nRCD": 9,  "nRP": 9,  "nRAS": 24, "nRC": 33, "nWR": 10, "nRTP": 5, "nCWL": 7,  "nCCD": 4, "nWTR": 5, "nCS": 2, "tCK_ps": 1500},
    "DDR3_1600H": {"rate": 1600, "nBL": 4, "nCL": 9,  "nRCD": 9,  "nRP": 9,  "nRAS": 28, "nRC": 37, "nWR": 12, "nRTP": 6, "nCWL": 8,  "nCCD": 4, "nWTR": 6, "nCS": 2, "tCK_ps": 1250},
    "DDR3_1600J": {"rate": 1600, "nBL": 4, "nCL": 10, "nRCD": 10, "nRP": 10, "nRAS": 28, "nRC": 38, "nWR": 12, "nRTP": 6, "nCWL": 8,  "nCCD": 4, "nWTR": 6, "nCS": 2, "tCK_ps": 1250},
    "DDR3_1600K": {"rate": 1600, "nBL": 4, "nCL": 11, "nRCD": 11, "nRP": 11, "nRAS": 28, "nRC": 39, "nWR": 12, "nRTP": 6, "nCWL": 8,  "nCCD": 4, "nWTR": 6, "nCS": 2, "tCK_ps": 1250},
    "DDR3_1866K": {"rate": 1866, "nBL": 4, "nCL": 11, "nRCD": 11, "nRP": 11, "nRAS": 32, "nRC": 43, "nWR": 14, "nRTP": 7, "nCWL": 9,  "nCCD": 4, "nWTR": 7, "nCS": 2, "tCK_ps": 1071},
    "DDR3_1866L": {"rate": 1866, "nBL": 4, "nCL": 12, "nRCD": 12, "nRP": 12, "nRAS": 32, "nRC": 44, "nWR": 14, "nRTP": 7, "nCWL": 9,  "nCCD": 4, "nWTR": 7, "nCS": 2, "tCK_ps": 1071},
    "DDR3_2133L": {"rate": 2133, "nBL": 4, "nCL": 12, "nRCD": 12, "nRP": 12, "nRAS": 36, "nRC": 48, "nWR": 16, "nRTP": 8, "nCWL": 10, "nCCD": 4, "nWTR": 8, "nCS": 2, "tCK_ps": 937},
    "DDR3_2133M": {"rate": 2133, "nBL": 4, "nCL": 13, "nRCD": 13, "nRP": 13, "nRAS": 36, "nRC": 49, "nWR": 16, "nRTP": 8, "nCWL": 10, "nCCD": 4, "nWTR": 8, "nCS": 2, "tCK_ps": 937},
}
