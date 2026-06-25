import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM2(DRAMStandard):
    name = "HBM2"
    internal_prefetch_size = 4       
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name -> init state) ----
    levels = {
        "Channel":        "N_A",
        "PseudoChannel":  "N_A",
        "BankGroup":      "N_A",
        "Bank":           "Closed",
        "Row":            "Closed",
        "Column":         "N_A",
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
    # Bus occupancy constraints are auto-generated from command_cycles + bus classification.
    timing_constraints = [
        # ============================================================
        # PseudoChannel — per-PC timing (independent per pseudo channel)
        # Multi-cycle adjustment applied automatically.
        # ============================================================

        # Data bus occupancy (per PC — separate DQ halves)
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # CAS timing (different bank group)
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        # Read-to-write turnaround
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + 2 - nCWL"),
        # Write-to-read turnaround
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # CAS to PREab
        TimingConstraint(level="PseudoChannel", preceding=["RD"], following=["PREab"], latency="nRTPL"),
        TimingConstraint(level="PseudoChannel", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # RAS timing
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # RAS to REF
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTPL"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab"], latency="nRFC"),
        # REFSB-to-ACT different bank (tRREFD)
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        # ACT-to-REFSB different bank (same as tRRD)
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),

        # ============================================================
        # BankGroup — same-group CAS timing
        # ============================================================
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # Same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),

        # ============================================================
        # Bank — single-bank timing
        # ============================================================
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
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nRREFD"] = cls._resolve_nRREFD(timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            timing_dict["tCK_ps"],
            org_dict.get("pseudochannel", 1)
            * org_dict.get("bankgroup", 1)
            * org_dict.get("bank", 1),
        )

    @staticmethod
    def _resolve_nRRDS(tCK_ps):
        # HBM2 tRRDS = 4 ns (JESD235D)
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(tCK_ps):
        # HBM2 tRRDL (same bank group) = 4 ns
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(tCK_ps):
        # HBM2 tFAW — vendor-specific, typical ~15 ns
        return max(8, math.ceil(15_000 / tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        # HBM2 tRFC (all-bank) from JESD235D — per pseudo channel density
        if density <= 1024:    tRFC_ns = 110
        elif density <= 2048:  tRFC_ns = 160
        elif density <= 4096:  tRFC_ns = 260
        elif density <= 8192:  tRFC_ns = 350
        else:                  tRFC_ns = 450
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(density, tCK_ps):
        # HBM2 tRFCSB (single-bank refresh)
        if density <= 8192:  tRFC_ns = 160
        else:                tRFC_ns = 200
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM2 tRREFD = 8 ns (min REFSB-to-ACT different bank)
        return max(4, math.ceil(8_000 / tCK_ps))

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # HBM2 tREFI = 3900 ns (3.9 us, base rate for 32 ms / 8192 rows)
        return math.ceil(3_900_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks):
        # HBM2 tREFISB = tREFI / num_banks
        trefi = 3_900_000  # ps
        return math.ceil(trefi / num_banks / tCK_ps)


# ---- HBM2 preset data ----
HBM2.org_presets = {
    "HBM2_1Gb":  {"density": 1024, "dq": 64, "channel_width": 64, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<13, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_2Gb":  {"density": 2048, "dq": 64, "channel_width": 64, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_4Gb":  {"density": 4096, "dq": 64, "channel_width": 64, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_8Gb":  {"density": 8192, "dq": 64, "channel_width": 64, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<16, "column": (1<<5) << 2},  # HBM CA already takes BL into account
}

# Timing presets — primary timings only.
# Secondary timings (nRRDS, nRRDL, nFAW, nRFC, nRFCpb, nRREFD, nREFI, nREFIpb)
# resolved from JEDEC tables in resolve_secondary_timings().
HBM2.timing_presets = {
    "HBM2_1600Mbps": {
        "rate": 1600, "nBL": 2, "nCL": 10, "nRCDRD": 10, "nRCDWR": 8,
        "nRP": 10, "nRAS": 24, "nRC": 34, "nWR": 12, "nRTPL": 4, "nCWL": 4,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 5, "nWTRL": 6,
        "tCK_ps": 1250,
    },
    "HBM2_2000Mbps": {
        "rate": 2000, "nBL": 2, "nCL": 14, "nRCDRD": 14, "nRCDWR": 12,
        "nRP": 14, "nRAS": 34, "nRC": 48, "nWR": 16, "nRTPL": 5, "nCWL": 5,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 6, "nWTRL": 8,
        "tCK_ps": 1000,
    },
    # HBM2_3800Mbps — between HBM2E 3.6 and OC 4.0 Gbps; common bin
    # for HBM2E silicon in Samsung Flashbolt.
    "HBM2_3800Mbps": {
        "rate": 3800, "nBL": 2, "nCL": 27, "nRCDRD": 27, "nRCDWR": 22,
        "nRP": 27, "nRAS": 63, "nRC": 90, "nWR": 30, "nRTPL": 10, "nCWL": 10,
        "nCCDS": 2, "nCCDL": 6, "nWTRS": 12, "nWTRL": 16,
        "tCK_ps": 526,
    },
    "HBM2_2400Mbps": {
        "rate": 2400, "nBL": 2, "nCL": 17, "nRCDRD": 17, "nRCDWR": 14,
        "nRP": 17, "nRAS": 40, "nRC": 57, "nWR": 19, "nRTPL": 6, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 8, "nWTRL": 10,
        "tCK_ps": 833,
    },
}
