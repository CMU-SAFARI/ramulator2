import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM1(DRAMStandard):
    name = "HBM1"
    internal_prefetch_size = 2       # BL2
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name -> init state) ----
    levels = {
        "Channel":      "N_A",
        "BankGroup":    "N_A",
        "Bank":         "Closed",
        "Row":          "Closed",
        "Column":       "N_A",
    }

    # ---- Commands ----
    commands = [
        "ACT", "PREpb", "PREab",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
    ]

    # ---- CA bus cycle count per command ----
    command_cycles = {"ACT": 2}

    # ---- Bus classification (dual command bus) ----
    row_commands = ["ACT", "PREpb", "PREab", "REFab", "REFpb"]
    column_commands = ["RD", "WR", "RDA", "WRA"]

    # ---- States ----
    states = ["Opened", "Closed", "N_A"]

    # ---- Timing parameters ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCDRD", "nRCDWR",
        "nRP", "nRAS", "nRC", "nWR", "nRTPL", "nCWL",
        "nCCDS", "nCCDL", "nRRDS", "nRRDL",
        "nWTRS", "nWTRL",
        "nFAW", "nRFC", "nRFCpb", "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
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

        # Channel — CAS timing (replaces Rank level — no rank in HBM)
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        # Channel — read-to-write turnaround
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + 2 - nCWL"),
        # Channel — write-to-read turnaround
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # Channel — CAS to PREab
        TimingConstraint(level="Channel", preceding=["RD"], following=["PREab"], latency="nRTPL"),
        TimingConstraint(level="Channel", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Channel — RAS timing
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # Channel — RAS to REF
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Channel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTPL"),
        TimingConstraint(level="Channel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Channel", preceding=["REFab"], following=["ACT", "PREab"], latency="nRFC"),
        # Channel — REFSB-to-ACT different bank (tRREFD)
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        # Channel — ACT-to-REFSB different bank (same as tRRD)
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),

        # BankGroup — same-group CAS timing
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # BankGroup — same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),

        # Bank — single-bank timing
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA"], latency="nRCDRD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["WR", "WRA"], latency="nRCDWR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTPL"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTPL + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — per-bank refresh (REFSB)
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        timing_dict["nRRDS"] = cls._resolve_nRRDS(timing_dict["tCK_ps"])
        timing_dict["nRRDL"] = cls._resolve_nRRDL(timing_dict["tCK_ps"])
        timing_dict["nFAW"] = cls._resolve_nFAW(timing_dict["tCK_ps"])
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(timing_dict["tCK_ps"])
        timing_dict["nRREFD"] = cls._resolve_nRREFD(timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            timing_dict["tCK_ps"],
            org_dict.get("bankgroup", 1) * org_dict.get("bank", 1),
        )

    @staticmethod
    def _resolve_nRRDS(tCK_ps):
        # HBM1 tRRDS = 4 ns (JESD235D)
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(tCK_ps):
        # HBM1 tRRDL (same bank group) = 4 ns
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(tCK_ps):
        # HBM1 tFAW — vendor-specific, typical ~15-20 ns
        return max(8, math.ceil(15_000 / tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        # HBM1 tRFC (all-bank) from JESD235D
        if density <= 1024:    tRFC_ns = 110
        elif density <= 2048:  tRFC_ns = 160
        elif density <= 4096:  tRFC_ns = 260
        else:                  tRFC_ns = 260
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(tCK_ps):
        # HBM1 tRFCSB = 160 ns (single-bank refresh)
        return math.ceil(160_000 / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM1 tRREFD = 8 ns (min REFSB-to-ACT different bank)
        return max(4, math.ceil(8_000 / tCK_ps))

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # HBM1 tREFI = 3900 ns (3.9 µs, base rate for 32 ms / 8192 rows)
        return math.ceil(3_900_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks):
        # HBM1 tREFISB = tREFI / num_banks
        trefi = 3_900_000  # ps
        return math.ceil(trefi / num_banks / tCK_ps)


# ---- HBM1 presets ----
HBM1.org_presets = {
    "HBM1_1Gb":  {"density": 1024, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<13, "column": (1<<6) << 1},    # HBM CA already takes BL into account
    "HBM1_2Gb":  {"density": 2048, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<14, "column": (1<<6) << 1},    # HBM CA already takes BL into account
    "HBM1_4Gb":  {"density": 4096, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<15, "column": (1<<6) << 1},    # HBM CA already takes BL into account
    # HBM1_8Gb — late HBM1 dies that doubled per-die density before
    # the HBM2 transition. nRFC table tops at 4 Gb so 8 Gb uses the
    # same tRFCab budget as 4 Gb (acceptable for non-refresh studies
    # since the spec didn't formally raise tRFC for the 8 Gb die).
    "HBM1_8Gb":  {"density": 8192, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<16, "column": (1<<6) << 1},
}

# Timing presets — primary timings only.
# Secondary timings (nRRDS, nRRDL, nFAW, nRFC, nRFCpb, nRREFD, nREFI, nREFIpb)
# resolved from JEDEC tables in resolve_secondary_timings().
HBM1.timing_presets = {
    "HBM1_1Gbps": {
        "rate": 1000, "nBL": 1, "nCL": 7, "nRCDRD": 7, "nRCDWR": 6,
        "nRP": 7, "nRAS": 17, "nRC": 24, "nWR": 8, "nRTPL": 4, "nCWL": 4,
        "nCCDS": 1, "nCCDL": 2, "nWTRS": 3, "nWTRL": 4,
        "tCK_ps": 2000,
    },
    "HBM1_2Gbps": {
        "rate": 2000, "nBL": 1, "nCL": 14, "nRCDRD": 14, "nRCDWR": 12,
        "nRP": 14, "nRAS": 34, "nRC": 48, "nWR": 16, "nRTPL": 5, "nCWL": 5,
        "nCCDS": 1, "nCCDL": 2, "nWTRS": 6, "nWTRL": 8,
        "tCK_ps": 1000,
    },
}
