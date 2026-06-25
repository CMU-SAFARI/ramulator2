import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class DDR5(DRAMStandard):
    name = "DDR5"
    internal_prefetch_size = 16      
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name → init state) ----
    levels = {
        "Channel":      "N_A",
        "Rank":         "N_A",
        "BankGroup":    "N_A",
        "Bank":         "Closed",
        "Row":          "Closed",
        "Column":       "N_A",
    }

    # ---- Commands ----
    commands = ["ACT", "PREpb", "PREab", "RD", "WR", "RDA", "WRA", "REFab"]

    # ---- CA bus cycle count per command (JEDEC Table 30) ----
    # 2-cycle commands carry address bits that don't fit in 1 CA cycle.
    # The DRAM only begins internal operation after the 2nd cycle.
    # Multi-cycle adjustment applied automatically in to_config().
    command_cycles = {"ACT": 2, "RD": 2, "RDA": 2, "WR": 2, "WRA": 2}

    # ---- States ----
    states = ["Opened", "Closed", "N_A"]

    # ---- Timing parameters (C++ Timing enum order) ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCD", "nRP", "nRAS", "nRC",
        "nWR", "nRTP", "nCWL", "nPPD",
        "nCCDS", "nCCDL", "nCCDS_WR", "nCCDL_WR",
        "nRRDS", "nRRDL", "nWTRS", "nWTRL",
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

        # Rank — CAS read timing (different bank group)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        # Rank — CAS write timing (different bank group, DDR5 separates read/write)
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS_WR"),
        # Rank — read-to-write turnaround (tRPST=0.5→2 tCK, tWPRE=2 tCK)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + 2 - nCWL + 2"),
        # Rank — write-to-read turnaround
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # Rank — sibling (rank switching)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA", "WR", "WRA"], latency="nBL + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nCS - nCL", window=1, sibling=True),
        # Rank — CAS to PREab
        TimingConstraint(level="Rank", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Rank — RAS timing
        TimingConstraint(level="Rank", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Rank", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # Rank — precharge-to-precharge delay (DDR5-specific nPPD)
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        # Rank — RAS to REF
        TimingConstraint(level="Rank", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT", "PREab"], latency="nRFC"),

        # BankGroup — same-group read CAS timing
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        # BankGroup — same-group write CAS timing (DDR5: separate nCCDL_WR)
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL_WR"),
        # BankGroup — same-group write-to-read
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # BankGroup — same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),

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
        timing_dict["nRRDS"] = cls._resolve_nRRDS(org_dict["dq"], timing_dict["rate"])
        timing_dict["nRRDL"] = cls._resolve_nRRDL(org_dict["dq"], timing_dict["rate"])
        timing_dict["nFAW"] = cls._resolve_nFAW(org_dict["dq"], timing_dict["rate"])
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nRRDS(dq, rate):
        # DDR5 nRRD_S from JEDEC tables (nCK)
        # dq → row index: x4=0, x8=1, x16=2
        _table = {
            #        3200  4800
            (4,  3200): 8, (4,  4800): 8,
            (8,  3200): 8, (8,  4800): 8,
            (16, 3200): 8, (16, 4800): 8,
        }
        val = _table.get((dq, rate))
        if val is not None:
            return val
        # Fallback for rates not in table: tRRD_S = 5 ns minimum
        tCK_ps = 2_000_000 / rate
        return max(8, math.ceil(5 * 1000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(dq, rate):
        # DDR5 nRRD_L from JEDEC tables (nCK)
        _table = {
            #        3200   4800
            (4,  3200): 5, (4,  4800): 12,
            (8,  3200): 5, (8,  4800): 12,
            (16, 3200): 5, (16, 4800): 12,
        }
        val = _table.get((dq, rate))
        if val is not None:
            return val
        # Fallback for rates not in table: tRRD_L = 5 ns minimum
        tCK_ps = 2_000_000 / rate
        return max(8, math.ceil(5 * 1000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(dq, rate):
        # DDR5 tFAW: ~20 ns for 1KB page, ~25 ns for 2KB page
        if dq <= 8:
            tFAW_ns = 20
        else:
            tFAW_ns = 25
        tCK_ps = 2_000_000 / rate
        return max(32, math.ceil(tFAW_ns * 1000 / tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        # DDR5 tRFC1 (all-bank refresh) from JEDEC
        if density <= 8192:    tRFC_ns = 195
        elif density <= 16384: tRFC_ns = 295
        elif density <= 32768: tRFC_ns = 410
        else: return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # DDR5 tREFI = 3900 ns (half of DDR4's 7800 ns)
        return math.ceil(3_900_000 / tCK_ps)


# ---- DDR5 JEDEC preset data ----

DDR5.org_presets = {
    "DDR5_8Gb_x4":   {"density": 8192,  "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 2, "row": 1<<16, "column": 1<<11},
    "DDR5_8Gb_x8":   {"density": 8192,  "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 2, "row": 1<<16, "column": 1<<10},
    "DDR5_8Gb_x16":  {"density": 8192,  "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 2, "row": 1<<16, "column": 1<<10},
    "DDR5_16Gb_x4":  {"density": 16384, "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<16, "column": 1<<11},
    "DDR5_16Gb_x8":  {"density": 16384, "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<16, "column": 1<<10},
    "DDR5_16Gb_x16": {"density": 16384, "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<16, "column": 1<<10},
    "DDR5_32Gb_x4":  {"density": 32768, "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<17, "column": 1<<11},
    "DDR5_32Gb_x8":  {"density": 32768, "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<17, "column": 1<<10},
    "DDR5_32Gb_x16": {"density": 32768, "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<17, "column": 1<<10},
}

# Primary timings only — secondary timings (nRRDS, nRRDL, nFAW, nRFC, nREFI)
# are resolved from JEDEC tables in resolve_secondary_timings().
DDR5.timing_presets = {
    # DDR5-3200 (tCK = 625 ps)
    "DDR5_3200AN": {"rate": 3200, "nBL": 8, "nCL": 24, "nRCD": 24, "nRP": 24, "nRAS": 52, "nRC": 76,  "nWR": 48, "nRTP": 12, "nCWL": 22, "nPPD": 2, "nCCDS": 8,  "nCCDL": 8,  "nCCDS_WR": 8,  "nCCDL_WR": 32, "nWTRS": 6,  "nWTRL": 16, "nCS": 2, "tCK_ps": 625},
    "DDR5_3200BN": {"rate": 3200, "nBL": 8, "nCL": 26, "nRCD": 26, "nRP": 26, "nRAS": 52, "nRC": 78,  "nWR": 48, "nRTP": 12, "nCWL": 24, "nPPD": 2, "nCCDS": 8,  "nCCDL": 8,  "nCCDS_WR": 8,  "nCCDL_WR": 32, "nWTRS": 6,  "nWTRL": 16, "nCS": 2, "tCK_ps": 625},
    "DDR5_3200C":  {"rate": 3200, "nBL": 8, "nCL": 28, "nRCD": 28, "nRP": 28, "nRAS": 52, "nRC": 80,  "nWR": 48, "nRTP": 12, "nCWL": 26, "nPPD": 2, "nCCDS": 8,  "nCCDL": 8,  "nCCDS_WR": 8,  "nCCDL_WR": 32, "nWTRS": 6,  "nWTRL": 16, "nCS": 2, "tCK_ps": 625},
    # DDR5-4800 (tCK = 416 ps)
    "DDR5_4800AN": {"rate": 4800, "nBL": 8, "nCL": 34, "nRCD": 34, "nRP": 34, "nRAS": 77, "nRC": 111, "nWR": 72, "nRTP": 18, "nCWL": 32, "nPPD": 2, "nCCDS": 8,  "nCCDL": 12, "nCCDS_WR": 8,  "nCCDL_WR": 48, "nWTRS": 6,  "nWTRL": 24, "nCS": 2, "tCK_ps": 416},
    "DDR5_4800BN": {"rate": 4800, "nBL": 8, "nCL": 36, "nRCD": 36, "nRP": 36, "nRAS": 77, "nRC": 113, "nWR": 72, "nRTP": 18, "nCWL": 34, "nPPD": 2, "nCCDS": 8,  "nCCDL": 12, "nCCDS_WR": 8,  "nCCDL_WR": 48, "nWTRS": 6,  "nWTRL": 24, "nCS": 2, "tCK_ps": 416},
    "DDR5_4800C":  {"rate": 4800, "nBL": 8, "nCL": 38, "nRCD": 38, "nRP": 38, "nRAS": 77, "nRC": 115, "nWR": 72, "nRTP": 18, "nCWL": 36, "nPPD": 2, "nCCDS": 8,  "nCCDL": 12, "nCCDS_WR": 8,  "nCCDL_WR": 48, "nWTRS": 6,  "nWTRL": 24, "nCS": 2, "tCK_ps": 416},
    # DDR5-5600 (tCK = 357 ps)
    "DDR5_5600AN": {"rate": 5600, "nBL": 8, "nCL": 40, "nRCD": 40, "nRP": 40, "nRAS": 90, "nRC": 130, "nWR": 84, "nRTP": 20, "nCWL": 38, "nPPD": 2, "nCCDS": 8,  "nCCDL": 12, "nCCDS_WR": 8,  "nCCDL_WR": 56, "nWTRS": 5,  "nWTRL": 28, "nCS": 2, "tCK_ps": 357},
    # DDR5-6400 (tCK = 312 ps)
    "DDR5_6400AN": {"rate": 6400, "nBL": 8, "nCL": 46, "nRCD": 46, "nRP": 46, "nRAS": 103, "nRC": 149, "nWR": 96, "nRTP": 24, "nCWL": 44, "nPPD": 2, "nCCDS": 8,  "nCCDL": 16, "nCCDS_WR": 8,  "nCCDL_WR": 64, "nWTRS": 5,  "nWTRL": 32, "nCS": 2, "tCK_ps": 312},
    # DDR5-7200 (tCK = 278 ps) — JEDEC top-bin grade. Used by next-gen
    # client and DC platforms (Granite Rapids/Turin tier).
    "DDR5_7200AN": {"rate": 7200, "nBL": 8, "nCL": 52, "nRCD": 52, "nRP": 52, "nRAS": 115, "nRC": 167, "nWR": 108, "nRTP": 27, "nCWL": 50, "nPPD": 2, "nCCDS": 8,  "nCCDL": 18, "nCCDS_WR": 8,  "nCCDL_WR": 72, "nWTRS": 5,  "nWTRL": 36, "nCS": 2, "tCK_ps": 278},
}
