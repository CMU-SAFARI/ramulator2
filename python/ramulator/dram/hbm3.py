import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM3(DRAMStandard):
    name = "HBM3"
    internal_prefetch_size = 8       # BL8
    tick_multiplier = 2              # 1 tick = half CK (models half-cycle row cmds)
    read_latency = "nCL + nBL"

    # ---- Hierarchy (level name -> init state) ----
    levels = {
        "Channel":        "N_A",
        "PseudoChannel":  "N_A",
        "Sid":            "N_A",
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
        "nCCDS", "nCCDL", "nCCDR", "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW", "nPPD",
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

        # Read-to-write turnaround
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
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
        # PRE-to-PRE delay (tPPD, new in HBM3)
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        # RAS to REF
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab"], latency="nRFC"),
        # REFpb-to-REFpb and REFpb-to-ACT different bank (tRREFD)
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
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
        # SID — same-SID and sibling-SID CAS timing
        # ============================================================
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDR", sibling=True),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDR", sibling=True),

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
        density = org_dict["density"]
        sid_count = org_dict.get("sid", 1)
        channel_density = density / sid_count
        timing_dict.setdefault("nRFC", cls._resolve_nRFC(channel_density, tCK_ps))
        timing_dict.setdefault("nRFMab", timing_dict["nRFC"])     # RFMab = same as REFab
        timing_dict.setdefault("nRFMpb", timing_dict["nRFCpb"])   # RFMpb = same as REFpb
        timing_dict.setdefault("nRREFD", cls._resolve_nRREFD(tCK_ps))
        timing_dict.setdefault("nREFI", cls._resolve_nREFI(tCK_ps))
        timing_dict.setdefault(
            "nREFIpb",
            cls._resolve_nREFIpb(
                tCK_ps,
                org_dict.get("bank", 1),
                org_dict.get("bankgroup", 1),
                org_dict.get("sid", 1),
            ),
        )

    @staticmethod
    def _resolve_nRFC(channel_density, tCK_ps):
        # HBM3 tRFCab (all-bank) — channel density in Mb, derived from die density / SID count.
        # Table 84 in JESD238
        if channel_density <= 4096:     tRFC_ns = 260
        elif channel_density <= 8192:   tRFC_ns = 350
        elif channel_density <= 16384:  tRFC_ns = 450
        else:                           tRFC_ns = 550
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
    def _resolve_nREFIpb(tCK_ps, num_banks, num_bankgroups, num_sids):
        # HBM3 tREFIpb = tREFI / num_banks
        trefi = 3_900_000  # ps
        return math.ceil(trefi / num_banks / num_bankgroups / num_sids / tCK_ps)


# ---- HBM3 JEDEC data (Table 4 in JESD238) ----
HBM3.org_presets = {
    # die density = 4 Gb, channel density = 4 Gb
    "HBM3_4Gb":  {"density": 4096, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 1, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    # die density = 8 Gb, channel density = 4 Gb
    "HBM3_8Gb_8hi":  {"density": 8192, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1<<13, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    # die density = 16 Gb, channel density = 8 Gb
    "HBM3_16Gb_8hi": {"density": 16384, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    # die density = 32 Gb, channel density = 16 Gb
    "HBM3_32Gb_8hi": {"density": 32768, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    # die density = 32 Gb, channel density = 8 Gb
    "HBM3_32Gb_16hi": {"density": 32768, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 4, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": (1<<5) << 3},  # HBM CA already takes BL into account
}

# Timing presets — CK cycles. Non-refresh timing values are supplied directly
# here; refresh-related values may be supplied here or resolved below.
HBM3.timing_presets = {
    "HBM3_6400Mbps": {
        "rate": 6400, "nBL": 2, "nCL": 20, "nRCDRD": 31, "nRCDWR": 15,
        "nRP": 26, "nRAS": 45, "nRC": 72, "nWR": 33, "nRTP": 9, "nCWL": 10,
        "nCCDS": 2, "nCCDL": 4, "nCCDR": 3,
        "nRRDS": 4, "nRRDL": 5, "nFAW": 24,
        "nWTRS": 7, "nWTRL": 10, "nRTW": 20,
        "nRFCpb": 320, "nRREFD": 8, "nREFI": 6240,
        "nPPD": 2,
        "tCK_ps": 625,
    },
    # HBM3E speed grades. JEDEC HBM3 timings are fixed in ns and the
    # CK-domain values scale linearly with rate (= 1.25x / 1.4375x / 1.5x
    # of the 6400 Mbps reference). Min-bounded parameters (nCCDS=2,
    # nRREFD=8, nPPD=2) are floor-clamped in JEDEC and stay constant.
    # nBL=2 is the half-rate burst minimum.
    "HBM3_8000Mbps": {
        "rate": 8000, "nBL": 2, "nCL": 25, "nRCDRD": 39, "nRCDWR": 19,
        "nRP": 33, "nRAS": 57, "nRC": 90, "nWR": 42, "nRTP": 12, "nCWL": 13,
        "nCCDS": 2, "nCCDL": 5, "nCCDR": 4,
        "nRRDS": 5, "nRRDL": 7, "nFAW": 30,
        "nWTRS": 9, "nWTRL": 13, "nRTW": 25,
        "nRFCpb": 400, "nRREFD": 8, "nREFI": 7800,
        "nPPD": 2,
        "tCK_ps": 500,
    },
    "HBM3_9200Mbps": {
        "rate": 9200, "nBL": 2, "nCL": 29, "nRCDRD": 45, "nRCDWR": 22,
        "nRP": 37, "nRAS": 65, "nRC": 104, "nWR": 47, "nRTP": 13, "nCWL": 14,
        "nCCDS": 2, "nCCDL": 6, "nCCDR": 4,
        "nRRDS": 6, "nRRDL": 7, "nFAW": 35,
        "nWTRS": 10, "nWTRL": 14, "nRTW": 29,
        "nRFCpb": 460, "nRREFD": 8, "nREFI": 8970,
        "nPPD": 2,
        "tCK_ps": 435,
    },
    "HBM3_9600Mbps": {
        "rate": 9600, "nBL": 2, "nCL": 30, "nRCDRD": 47, "nRCDWR": 23,
        "nRP": 39, "nRAS": 68, "nRC": 108, "nWR": 50, "nRTP": 14, "nCWL": 15,
        "nCCDS": 2, "nCCDL": 6, "nCCDR": 5,
        "nRRDS": 6, "nRRDL": 8, "nFAW": 36,
        "nWTRS": 11, "nWTRL": 15, "nRTW": 30,
        "nRFCpb": 480, "nRREFD": 8, "nREFI": 9360,
        "nPPD": 2,
        "tCK_ps": 417,
    },
}
