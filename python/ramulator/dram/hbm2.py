import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM2(DRAMStandard):
    name = "HBM2"
    internal_prefetch_size = 4
    data_payload_bytes = 32  # One pseudochannel
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
        "nCCDS", "nCCDL", "nCCDR",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW", "nRFC", "nRFCpb", "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {"Read": "RD", "Write": "WR"}

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

        # Read-to-write turnaround
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        # Write-to-read turnaround
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # CAS to PREab
        # JESD235D Table 33 (PDF p.46): READ (w/AP) → PREALL tRTP; WRITE (w/AP) → PREALL WL + BL/2 + RU(tWR/tCK)
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["PREab"], latency="nRTPL"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # RAS timing
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT", "REFpb"], following=["ACT", "REFpb"], latency="nFAW", window=4, shared_window=True),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # RAS to REF
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTPL"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREpb", "PREab", "REFab", "REFpb"], latency="nRFC"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFab"], latency="nRFCpb"),
        # REFSB-to-ACT different bank (tRREFD)
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
        # ACT-to-REFSB different bank (same as tRRD)
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),

        # SID — same-SID and sibling-SID CAS timing
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDR", sibling=True),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS", sibling=True),

        # ============================================================
        # BankGroup — same-group CAS timing
        # ============================================================
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # Same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["REFpb"], latency="nRRDL"),

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
        # JESD235D Table 33 (PDF p.46): WRITE → READ w/AP same bank = WL + BL/2 + MAX[RU(tWR/tCK) − tRTP, tWTR]
        TimingConstraint(level="Bank", preceding=["WR"], following=["RDA"], latency="nCWL + nBL + max(nWR - nRTPL, nWTRL)"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT", "REFpb"], latency="nRTPL + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT", "REFpb"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — per-bank refresh (REFSB)
        # JESD235D §6.3.2.6 (PDF p.50) and Table 35 Note 3 (PDF p.51): a bank is refreshed again only in
        # the next set, which starts at least tRFCSB after the current set. HBMPerBankRefresh keeps the set order.
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT", "REFpb"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        channel_density = org_dict["channel_density"]
        timing_dict["nRC"] = timing_dict["nRAS"] + timing_dict["nRP"]
        timing_dict["nCCDS"] = 2
        timing_dict["nCCDL"] = max(4, math.ceil(2_800 / tCK_ps))
        timing_dict["nCCDR"] = cls._resolve_nCCDR(
            org_dict["sid"], timing_dict["nCCDS"]
        )
        timing_dict["nRTW"] = cls._resolve_nRTW(timing_dict, tCK_ps)
        timing_dict["nRRDS"] = cls._resolve_nRRDS(tCK_ps)
        timing_dict["nRRDL"] = cls._resolve_nRRDL(tCK_ps)
        timing_dict["nFAW"] = cls._resolve_nFAW(tCK_ps)
        timing_dict["nRFC"] = cls._resolve_nRFC(channel_density, tCK_ps)
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(
            org_dict["die_density"], tCK_ps
        )
        timing_dict["nRREFD"] = cls._resolve_nRREFD(tCK_ps)
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            tCK_ps,
            org_dict["sid"] * org_dict.get("bankgroup", 1) * org_dict.get("bank", 1),
        )

    @staticmethod
    def _resolve_nCCDR(num_sids, nCCDS):
        if num_sids == 1:
            return nCCDS
        return {
            # === Ramulator Guesstimate ===
            # Table 68 Note 22 (PDF p.120): tCCDR is vendor specific, 2 to 4 nCK
            2: 2,
            # =============================
        }.get(num_sids, -1)

    @staticmethod
    def _resolve_nRTW(timing_dict, tCK_ps):
        # JESD235D Tables 67 and 68, Note 23.
        tdqsq_max_ps = {1_600: 106, 2_000: 85, 2_400: 71}.get(timing_dict["rate"])
        if tdqsq_max_ps is None:
            return -1
        base_cycles = timing_dict["nCL"] + timing_dict["nBL"] - timing_dict["nCWL"]
        analog_numerator = 3 * tCK_ps + 10 * (3_500 + tdqsq_max_ps)
        return base_cycles + math.ceil(analog_numerator / (10 * tCK_ps))

    # === Ramulator Guesstimate ===
    @staticmethod
    def _resolve_nRRDS(tCK_ps):
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nRRDL(tCK_ps):
        return max(4, math.ceil(4_000 / tCK_ps))

    @staticmethod
    def _resolve_nFAW(tCK_ps):
        return max(8, math.ceil(15_000 / tCK_ps))
    # =============================

    @staticmethod
    def _resolve_nRFC(channel_density, tCK_ps):
        # JESD235D Table 68: tRFC is selected by density per channel.
        tRFC_ns = {2048: 160, 4096: 260, 8192: 350, 16384: 450}.get(channel_density)
        if tRFC_ns is None:
            return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(die_density, tCK_ps):
        # JESD235D Table 68, Note 34: tRFCSB is selected by die density.
        tRFCpb_ns = {
            1024: 160,
            2048: 160,
            4096: 160,
            8192: 160,
            12288: 200,
            16384: 200,
        }.get(die_density)
        if tRFCpb_ns is None:
            return -1
        return math.ceil(tRFCpb_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # JESD235D Table 68: tRREFD = 8 ns.
        return math.ceil(8_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # JESD235D Table 68: tREFI(max) = 3.9 us.
        return 3_900_000 // tCK_ps

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks):
        # JESD235D Table 68, Note 29: tREFISB(max) = tREFI / N,
        # where N is the number of banks in one pseudochannel.
        return 3_900_000 // (num_banks * tCK_ps)


# ---- HBM2 preset data ----
# die_density and stack_height: two channels per die, 4-High stack (Table 4 Note 7, PDF p.15, leaves this to the vendor)
HBM2.org_presets = {
    "HBM2_1Gb":  {"pseudochannel_density": 1024, "channel_density": 2048, "die_density": 4096, "stack_height": 4, "dq": 64, "channel_width": 128, "pseudochannel": 2, "sid": 1, "bankgroup": 4, "bank": 2, "row": 1<<14, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_2Gb":  {"pseudochannel_density": 2048, "channel_density": 4096, "die_density": 8192, "stack_height": 4, "dq": 64, "channel_width": 128, "pseudochannel": 2, "sid": 1, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_4Gb":  {"pseudochannel_density": 4096, "channel_density": 8192, "die_density": 16384, "stack_height": 4, "dq": 64, "channel_width": 128, "pseudochannel": 2, "sid": 1, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": (1<<5) << 2},  # HBM CA already takes BL into account
    "HBM2_8Gb":  {"pseudochannel_density": 8192, "channel_density": 16384, "die_density": 16384, "stack_height": 8, "dq": 64, "channel_width": 128, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1<<15, "column": (1<<5) << 2},  # HBM CA already takes BL into account
}

# Organization-dependent and derived timings are resolved below.
HBM2.timing_presets = {
    "HBM2_1600Mbps": {
        "rate": 1600, "nBL": 2,
        # === Ramulator Guesstimate ===
        # Same ns as the 2.0 Gb/s bin, at 1250 ps
        "nCL": 12, "nRCDRD": 12, "nRCDWR": 10,
        "nRP": 12, "nRAS": 28, "nWR": 13,
        "nRTPL": 4, "nCWL": 4,
        "nWTRS": 5, "nWTRL": 7,
        # =============================
        "tCK_ps": 1250,
    },
    "HBM2_2000Mbps": {
        "rate": 2000, "nBL": 2,
        # === Ramulator Guesstimate ===
        "nCL": 14, "nRCDRD": 14, "nRCDWR": 12,
        "nRP": 14, "nRAS": 34, "nWR": 16,
        "nRTPL": 5, "nCWL": 5,
        "nWTRS": 6, "nWTRL": 8,
        # =============================
        "tCK_ps": 1000,
    },
    "HBM2_2400Mbps": {
        "rate": 2400, "nBL": 2,
        # === Ramulator Guesstimate ===
        "nCL": 17, "nRCDRD": 17, "nRCDWR": 14,
        "nRP": 17, "nRAS": 40, "nWR": 19,
        "nRTPL": 6, "nCWL": 6,
        "nWTRS": 8, "nWTRL": 10,
        # =============================
        "tCK_ps": 833,
    },
}
