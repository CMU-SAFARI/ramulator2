import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM3(DRAMStandard):
    name = "HBM3"
    channel_width = 32               
    internal_prefetch_size = 8       # BL8
    tick_multiplier = 2              # 1 tick = half CK (models half-cycle row cmds)
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
        "RFMab", "RFMpb",
    ]

    # ---- CA bus cycle count per command (in CK) ----
    # ACT = 1.5 CK (R-F-R). Half-cycle row commands = 0.5 CK.
    # Column commands default to 1 CK (not listed).
    command_cycles = {
        "ACT": 1.5,
        "PREpb": 0.5, "PREab": 0.5,
        "REFab": 0.5, "REFpb": 0.5,
        "RFMab": 0.5, "RFMpb": 0.5,
    }

    # ---- Bus classification (dual command bus) ----
    row_commands = ["ACT", "PREpb", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"]
    column_commands = ["RD", "WR", "RDA", "WRA"]

    # ---- States ----
    states = ["Opened", "Closed", "N_A"]

    # ---- Timing parameters ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCDRD", "nRCDWR",
        "nRP", "nRAS", "nRC", "nWR", "nRTP", "nCWL",
        "nCCDS", "nCCDL", "nRRDS", "nRRDL",
        "nWTRS", "nWTRL",
        "nFAW", "nPPD", "nBTR",
        "nRFC", "nRFCpb", "nRFMab", "nRFMpb",
        "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

    # ---- Timing constraints ----
    # Helper lists for readability
    _half_cycle_row = ["PREpb", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"]
    _all_row = ["ACT", "PREpb", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"]
    _all_col = ["RD", "WR", "RDA", "WRA"]

    timing_constraints = [
        # Bus occupancy constraints are auto-generated from command_cycles + bus classification.

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
        # Read-to-write turnaround (nBTR = bus turnaround guard, typically 2 CK)
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nCL + nBL + nBTR - nCWL"),
        # Write-to-read turnaround
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # CAS to PREab
        TimingConstraint(level="PseudoChannel", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # RAS timing
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # PREpb-to-PREpb delay (tPPD, new in HBM3)
        TimingConstraint(level="PseudoChannel", preceding=["PREpb"], following=["PREpb"], latency="nPPD"),
        # RAS to REF
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab"], latency="nRFC"),
        # REFpb-to-ACT different bank (tRREFD)
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        # ACT-to-REFpb different bank (same as tRRD)
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        # RFMab constraints (same structure as REFab)
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMab"], following=["ACT", "PREab"], latency="nRFMab"),
        # RFMpb constraints (same structure as REFpb)
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMpb"], latency="nRRDS"),

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
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — per-bank refresh
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),

        # Bank — per-bank refresh management
        TimingConstraint(level="Bank", preceding=["RFMpb"], following=["ACT"], latency="nRFMpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["RFMpb"], latency="nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        timing_dict["nRRDS"] = cls._resolve_nRRDS(tCK_ps)
        timing_dict["nRRDL"] = cls._resolve_nRRDL(tCK_ps)
        timing_dict["nFAW"] = cls._resolve_nFAW(tCK_ps)
        density = org_dict["density"]
        timing_dict["nRFC"] = cls._resolve_nRFC(density, tCK_ps)
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(density, tCK_ps)
        timing_dict["nRFMab"] = timing_dict["nRFC"]     # RFMab = same as REFab
        timing_dict["nRFMpb"] = timing_dict["nRFCpb"]   # RFMpb = same as REFpb
        timing_dict["nRREFD"] = cls._resolve_nRREFD(tCK_ps)
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        num_banks = (org_dict.get("pseudochannel", 1)
                     * org_dict.get("bankgroup", 1)
                     * org_dict.get("bank", 1))
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(tCK_ps, num_banks)

    @staticmethod
    def _resolve_nRRDS(tCK_ps):
        # HBM3 tRRDS ~ 5 ns (vendor-specific, different BG)
        return max(4, math.ceil(5_000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(tCK_ps):
        # HBM3 tRRDL ~ 5 ns (vendor-specific, same BG)
        return max(4, math.ceil(5_000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(tCK_ps):
        # HBM3 tFAW — vendor-specific, typical ~15 ns
        return max(8, math.ceil(15_000 / tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        # HBM3 tRFCab (all-bank) — per pseudo channel density in Mb
        if density <= 2048:    tRFC_ns = 260
        elif density <= 4096:  tRFC_ns = 350
        elif density <= 8192:  tRFC_ns = 450
        else:                  tRFC_ns = 550
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(density, tCK_ps):
        # HBM3 tRFCpb (per-bank refresh)
        if density <= 8192:  tRFC_ns = 200
        else:                tRFC_ns = 250
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM3 tRREFD = MAX(3*tCK, 8 ns)
        return max(3, math.ceil(8_000 / tCK_ps))

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # HBM3 tREFI = 3900 ns (3.9 us, 32 ms / 8192 rows)
        return math.ceil(3_900_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks):
        # HBM3 tREFIpb = tREFI / num_banks
        trefi = 3_900_000  # ps
        return math.ceil(trefi / num_banks / tCK_ps)


# ---- HBM3 JEDEC data ----
HBM3.org_presets = {
    # 16 banks (4 BG x 4 banks) — 4-High typical
    "HBM3_4Gb":  {"density": 2048, "dq": 32, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": 1<<5},
    "HBM3_8Gb":  {"density": 4096, "dq": 32, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": 1<<5},
    "HBM3_16Gb": {"density": 8192, "dq": 32, "pseudochannel": 2, "bankgroup": 4, "bank": 4, "row": 1<<16, "column": 1<<5},
    # 32 banks (8 BG x 4 banks) — 8-High typical
    "HBM3_8Gb_32B":  {"density": 4096, "dq": 32, "pseudochannel": 2, "bankgroup": 8, "bank": 4, "row": 1<<14, "column": 1<<5},
    "HBM3_16Gb_32B": {"density": 8192, "dq": 32, "pseudochannel": 2, "bankgroup": 8, "bank": 4, "row": 1<<15, "column": 1<<5},
}

# Timing presets — primary timings only (all in CK cycles).
# Secondary timings (nRRDS, nRRDL, nFAW, nRFC, nRFCpb, nRFMab, nRFMpb,
# nRREFD, nREFI, nREFIpb) resolved from JEDEC tables in
# resolve_secondary_timings().
HBM3.timing_presets = {
    "HBM3_4800Mbps": {
        "rate": 4800, "nBL": 2, "nCL": 18, "nRCDRD": 18, "nRCDWR": 10,
        "nRP": 18, "nRAS": 36, "nRC": 54, "nWR": 16, "nRTP": 4, "nCWL": 6,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 6, "nWTRL": 8,
        "nPPD": 2, "nBTR": 2,
        "tCK_ps": 833,
    },
    "HBM3_5600Mbps": {
        "rate": 5600, "nBL": 2, "nCL": 22, "nRCDRD": 22, "nRCDWR": 12,
        "nRP": 22, "nRAS": 42, "nRC": 64, "nWR": 20, "nRTP": 5, "nCWL": 8,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 8, "nWTRL": 10,
        "nPPD": 2, "nBTR": 2,
        "tCK_ps": 714,
    },
    "HBM3_6400Mbps": {
        "rate": 6400, "nBL": 2, "nCL": 26, "nRCDRD": 26, "nRCDWR": 14,
        "nRP": 26, "nRAS": 48, "nRC": 74, "nWR": 24, "nRTP": 6, "nCWL": 8,
        "nCCDS": 2, "nCCDL": 4, "nWTRS": 8, "nWTRL": 12,
        "nPPD": 2, "nBTR": 2,
        "tCK_ps": 625,
    },
}
