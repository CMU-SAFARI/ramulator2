import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class LPDDR5(DRAMStandard):
    name = "LPDDR5"
    internal_prefetch_size = 16      # BL16
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name -> init state) ----
    levels = {
        "Channel":      "N_A",
        "Rank":         "N_A",
        "BankGroup":    "N_A",
        "Bank":         "Closed",
        "Row":          "Closed",
        "Column":       "N_A",
    }

    # ---- Commands ----
    commands = [
        "ACT1", "ACT2", "PREpb", "PREab",
        "CAS_RD", "CAS_WR",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
    ]

    # ---- CA bus cycle count per command ----
    # LPDDR5 CA is DDR -> all commands are 1 nCK.
    # ACT-1 and ACT-2 are separate 1 nCK commands (interleaving allowed between them).
    command_cycles = {}

    # ---- States ----
    states = ["Opened", "Closed", "Activating", "N_A"]

    # ---- Timing parameters ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCD", "nRP", "nRPab", "nRAS", "nRC",
        "nWR", "nRTP", "nCWL", "nPPD",
        "nCCDS", "nCCDL", "nCCDS_WR", "nCCDL_WR",
        "nRRDS", "nRRDL", "nWTRS", "nWTRL",
        "nFAW", "nRFC", "nRFCpb", "nREFI", "nREFIpb",
        "nWCKPST",
        "nCAS",
        "nAAD",
        "nCS", "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

    # ---- Timing constraints ----
    timing_constraints = [
        # Bus occupancy constraints are auto-generated from command_cycles.
        # Channel — data bus occupancy
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Bank — CAS must immediately precede RD/WR (nCAS = 0 means next cycle)
        TimingConstraint(level="Bank", preceding=["CAS_RD"], following=["RD", "RDA"], latency="nCAS"),
        TimingConstraint(level="Bank", preceding=["CAS_WR"], following=["WR", "WRA"], latency="nCAS"),

        # Rank — CAS read timing (different bank group)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        # Rank — CAS write timing (different bank group)
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS_WR"),
        # Rank — read-to-write turnaround
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + 2 - nCWL"),
        # Rank — write-to-read turnaround
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # Rank — sibling (rank switching)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA", "WR", "WRA"], latency="nBL + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCL + nBL + nCS - nCWL", window=1, sibling=True),
        # Rank — CAS to PREab
        TimingConstraint(level="Rank", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Rank — RAS timing (all measured from ACT-1)
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nRRDS"),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT1"], latency="nRPab"),
        # Rank — precharge-to-precharge delay
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        # Rank — RAS to REF
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT1", "PREab"], latency="nRFC"),

        # BankGroup — same-group read CAS timing
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        # BankGroup — same-group write CAS timing
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL_WR"),
        # BankGroup — same-group write-to-read
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # BankGroup — same-group RAS timing (ACT-1 to ACT-1)
        TimingConstraint(level="BankGroup", preceding=["ACT1"], following=["ACT1"], latency="nRRDL"),

        # Bank — single-bank timing (all measured from ACT-1)
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["ACT1"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["RD", "RDA", "WR", "WRA"], latency="nRCD"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT1"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT1"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT1"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — per-bank refresh
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT1"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        timing_dict["nRRDS"] = cls._resolve_nRRDS(timing_dict["tCK_ps"])
        timing_dict["nRRDL"] = cls._resolve_nRRDL(timing_dict["tCK_ps"])
        timing_dict["nFAW"] = cls._resolve_nFAW(timing_dict["tCK_ps"])
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nRRDS(tCK_ps):
        # LPDDR5 tRRD (different BG) = 5 ns
        return max(4, math.ceil(5_000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(tCK_ps):
        # LPDDR5 tRRD (same BG) = 5 ns, minimum 4 nCK
        return max(4, math.ceil(5_000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(tCK_ps):
        # LPDDR5 BG mode tFAW = 20 ns
        return max(16, math.ceil(20_000 / tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        # LPDDR5 tRFCab from JESD209-5B
        if density <= 4096:    tRFC_ns = 130
        elif density <= 8192:  tRFC_ns = 210
        elif density <= 16384: tRFC_ns = 280
        else:                  tRFC_ns = 380
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(density, tCK_ps):
        # LPDDR5 tRFCpb (per-bank) from JESD209-5B
        if density <= 4096:    tRFC_ns = 60
        elif density <= 8192:  tRFC_ns = 120
        elif density <= 16384: tRFC_ns = 160
        else:                  tRFC_ns = 210
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # LPDDR5 tREFI = 3906 ns (refresh window 32 ms, 8192 refreshes)
        return math.ceil(3_906_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(tCK_ps):
        # LPDDR5 tREFIpb = 488 ns (tREFI / 8, per-bank interval)
        return math.ceil(488_000 / tCK_ps)


# ---- LPDDR5 JEDEC preset data ----

LPDDR5.org_presets = {
    "LPDDR5_8Gb_x16":  {"density": 8192,  "dq": 16, "channel_width": 16, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": 1<<10},
    "LPDDR5_16Gb_x16": {"density": 16384, "dq": 16, "channel_width": 16, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<16, "column": 1<<10},
}

# Primary timings only — secondary timings resolved by resolve_secondary_timings().
# All values in CK cycles (CKR 4:1).
LPDDR5.timing_presets = {
    # LPDDR5-6400 (tCK = 1250 ps, CK = 800 MHz)
    "LPDDR5_6400": {
        "rate": 6400, "nBL": 2, "nCL": 17, "nRCD": 15, "nRP": 15, "nRPab": 17,
        "nRAS": 34, "nRC": 49, "nWR": 28, "nRTP": 8, "nCWL": 9, "nPPD": 2,
        "nCCDS": 2, "nCCDL": 4, "nCCDS_WR": 2, "nCCDL_WR": 4,
        "nWTRS": 5, "nWTRL": 10, "nWCKPST": 1, "nCAS": 0,
        "nAAD": 8, "nCS": 2, "tCK_ps": 1250,
    },
    # LPDDR5X-8533 (1.333x of LPDDR5-6400) — JEDEC LPDDR5X first wave,
    # used by Snapdragon 8 Gen 3 / Dimensity 9300 era flagships.
    "LPDDR5_8533": {
        "rate": 8533, "nBL": 2, "nCL": 23, "nRCD": 20, "nRP": 20, "nRPab": 23,
        "nRAS": 46, "nRC": 66, "nWR": 38, "nRTP": 11, "nCWL": 12, "nPPD": 2,
        "nCCDS": 2, "nCCDL": 6, "nCCDS_WR": 2, "nCCDL_WR": 6,
        "nWTRS": 7, "nWTRL": 14, "nWCKPST": 1, "nCAS": 0,
        "nAAD": 11, "nCS": 2, "tCK_ps": 938,
    },
}
