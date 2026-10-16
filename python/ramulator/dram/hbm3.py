import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM3(DRAMStandard):
    name = "HBM3"
    internal_prefetch_size = 8       # BL8
    data_payload_bytes = 32          # One pseudochannel
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
        "nCCDS", "nCCDL", "nCCDR",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW", "nPPD",
        "nRFC", "nRFCpb", "nRFMab", "nRFMpb",
        "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {"Read": "RD", "Write": "WR"}

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
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # RAS timing
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT", "REFpb", "RFMpb"], following=["ACT", "REFpb", "RFMpb"], latency="nFAW", window=4, shared_window=True),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # PRE-to-PRE delay (tPPD, new in HBM3)
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        # RAS to REF
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        # JESD238 Table 35.
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], latency="nRFC"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFab", "RFMab"], latency="nRFCpb"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        # JESD238 Table 35 Note 2 (PDF p.65): a bank must be idle with tRP satisfied before it is refreshed
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["REFpb", "RFMpb"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMab"], following=["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], latency="nRFMab"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["REFab", "RFMab"], latency="nRFMpb"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMpb"], latency="nRRDS"),

        # ============================================================
        # SID — same-SID and sibling-SID CAS timing
        # ============================================================
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDR", sibling=True),
        # JESD238 Table 93 Note 17 (PDF p.178): seamless WRITEs across SIDs use tCCDS
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS", sibling=True),

        # ============================================================
        # BankGroup — same-group CAS timing
        # ============================================================
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # Same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["REFpb", "RFMpb"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["REFpb", "RFMpb"], following=["ACT"], latency="nRRDL"),

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
        # JESD238 Table 33 (PDF p.58): WRITE → READ w/AP same bank = WL + 2 + MAX[RU(tWR/tCK) − tRTP, tWTR]
        TimingConstraint(level="Bank", preceding=["WR"], following=["RDA"], latency="nCWL + nBL + max(nWR - nRTP, nWTRL)"),
        # JESD238 Table 33 Note 8 (PDF p.58): READ/WRITE w/AP → ACTIVATE or PER BANK REFRESH (same bank); RFMpb as REFpb (§6.3.2.7, PDF p.67)
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT", "REFpb", "RFMpb"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT", "REFpb", "RFMpb"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — per-bank refresh
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),

        # Bank — per-bank refresh management
        TimingConstraint(level="Bank", preceding=["RFMpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRFMpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["RFMpb"], latency="nRP"),
    ]

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        channel_density = org_dict["channel_density"]
        timing_dict["nRC"] = timing_dict["nRAS"] + timing_dict["nRP"]
        timing_dict["nCCDL"] = max(4, math.ceil(2_500 / tCK_ps))
        timing_dict["nCCDR"] = cls._resolve_nCCDR(
            org_dict["sid"], timing_dict["nCCDS"]
        )
        timing_dict["nRTW"] = cls._resolve_nRTW(timing_dict, tCK_ps)
        timing_dict["nRFC"] = cls._resolve_nRFC(
            org_dict["die_density"],
            org_dict["stack_height"],
            channel_density,
            tCK_ps,
        )
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(
            org_dict["die_density"],
            org_dict["stack_height"],
            tCK_ps,
        )
        timing_dict["nRFMab"] = timing_dict["nRFC"]
        timing_dict["nRFMpb"] = timing_dict["nRFCpb"]
        timing_dict["nRREFD"] = cls._resolve_nRREFD(tCK_ps)
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            tCK_ps,
            org_dict["bank"],
            org_dict["bankgroup"],
            org_dict["sid"],
        )

    @staticmethod
    def _resolve_nCCDR(num_sids, nCCDS):
        if num_sids == 1:
            return nCCDS
        return {
            # === Ramulator Guesstimate ===
            2: 3,
            4: 3,
            # =============================
        }.get(num_sids, -1)

    @staticmethod
    def _resolve_nRTW(timing_dict, tCK_ps):
        # JESD238 Tables 92 and 93, Note 18.
        base_cycles = timing_dict["nCL"] + timing_dict["nBL"] - timing_dict["nCWL"]
        analog_numerator = (
            max(-2_000, -2 * tCK_ps) + 5 * tCK_ps + 10 * (2_500 + 20)
        )
        return base_cycles + math.ceil(analog_numerator / (10 * tCK_ps))

    @staticmethod
    def _resolve_nRFC(
        die_density,
        stack_height,
        channel_density,
        tCK_ps,
    ):
        # JESD238 Table 93.
        tRFC_ns = {
            (8192, 8, 4096): 260,
            (8192, 12, 6144): 310,
            (8192, 16, 8192): 350,
            (16384, 4, 4096): 260,
            (16384, 8, 8192): 350,
            (16384, 12, 12288): 410,
            (16384, 16, 16384): 450,
            # === Ramulator Guesstimate ===
            (32768, 8, 16384): 450,
            (32768, 16, 32768): 550,
            # =============================
        }.get((die_density, stack_height, channel_density))
        if tRFC_ns is None:
            return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(die_density, stack_height, tCK_ps):
        # JESD238 Table 93 defines 16 Gb/die.
        if die_density == 16384:
            tRFCpb_ns = 200
        else:
            tRFCpb_ns = {
                # === Ramulator Guesstimate ===
                (8192, 8): 200,
                (32768, 8): 200,
                (32768, 16): 200,
                # =============================
            }.get((die_density, stack_height))
        if tRFCpb_ns is None:
            return -1
        return math.ceil(tRFCpb_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM3 tRREFD = MAX(3*tCK, 8 ns)
        return max(3, math.ceil(8_000 / tCK_ps))

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # JESD238 Table 93 specifies a maximum interval of 3.9 us.
        return 3_900_000 // tCK_ps

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks, num_bankgroups, num_sids):
        # JESD238 Table 93: tREFIpb = tREFI / banks per pseudo-channel.
        return 3_900_000 // (num_banks * num_bankgroups * num_sids * tCK_ps)


# ---- HBM3 JEDEC data (Table 4 in JESD238) ----
HBM3.org_presets = {
    # HBM CA already takes BL into account
    # One preset is one 64-bit JEDEC channel split into two 32-bit
    # pseudo-channels (JESD238 Table 4).
    "HBM3_16Gb_4hi":  {"die_density": 16384, "channel_density": 4096,  "stack_height": 4,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 1, "bankgroup": 4, "bank": 4, "row": 1 << 14, "column": (1 << 5) << 3},
    "HBM3_8Gb_8hi":   {"die_density": 8192,  "channel_density": 4096,  "stack_height": 8,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1 << 13, "column": (1 << 5) << 3},
    "HBM3_16Gb_8hi":  {"die_density": 16384, "channel_density": 8192,  "stack_height": 8,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1 << 14, "column": (1 << 5) << 3},
    "HBM3_32Gb_8hi":  {"die_density": 32768, "channel_density": 16384, "stack_height": 8,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 2, "bankgroup": 4, "bank": 4, "row": 1 << 15, "column": (1 << 5) << 3},
    "HBM3_32Gb_16hi": {"die_density": 32768, "channel_density": 32768, "stack_height": 16, "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 4, "bankgroup": 4, "bank": 4, "row": 1 << 15, "column": (1 << 5) << 3},
}

# Backward-compatible alias for the 4 Gb-per-channel organization.
HBM3.org_presets["HBM3_4Gb"] = HBM3.org_presets["HBM3_16Gb_4hi"]

# Timing presets — CK cycles. Direct speed-bin timings are supplied here;
# organization-dependent and derived refresh timings are resolved below.
HBM3.timing_presets = {
    "HBM3_6400Mbps": {
        "rate": 6400, "nBL": 2,
        "nCCDS": 2,
        # === Ramulator Guesstimate ===
        "nCL": 20, "nRCDRD": 31, "nRCDWR": 15,
        "nRP": 26, "nRAS": 45, "nWR": 33,
        "nRTP": 9, "nCWL": 10,
        "nRRDS": 4, "nRRDL": 5, "nFAW": 24,
        "nWTRS": 7, "nWTRL": 10,
        # =============================
        "nPPD": 2, "tCK_ps": 625,
    },
}
