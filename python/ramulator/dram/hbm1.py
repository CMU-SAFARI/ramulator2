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
        "nCCDS", "nCCDL",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW", "nRFC", "nRFCpb", "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
    ]

    # ---- External request types ----
    supported_requests = {"Read": "RD", "Write": "WR"}

    # ---- Timing constraints ----
    timing_constraints = [
        # Channel — data bus occupancy
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Channel — CAS timing (replaces Rank level — no rank in HBM)
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        # Channel — read-to-write turnaround
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        # Channel — write-to-read turnaround
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # Channel — CAS to PREab
        # JESD235D Table 33 (PDF p.46): READ (w/AP) → PREALL tRTP; WRITE (w/AP) → PREALL WL + BL/2 + RU(tWR/tCK)
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["PREab"], latency="nRTPL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Channel — RAS timing
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["ACT", "REFpb"], following=["ACT", "REFpb"], latency="nFAW", window=4, shared_window=True),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        # Channel — RAS to REF
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Channel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTPL"),
        TimingConstraint(level="Channel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Channel", preceding=["REFab"], following=["ACT", "PREpb", "PREab", "REFab", "REFpb"], latency="nRFC"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFab"], latency="nRFCpb"),
        # Channel — REFSB-to-ACT different bank (tRREFD)
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
        # Channel — ACT-to-REFSB different bank (same as tRRD)
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),

        # BankGroup — same-group CAS timing
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # BankGroup — same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["REFpb"], latency="nRRDL"),

        # Bank — single-bank timing
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
        timing_dict["nCCDS"] = 1
        timing_dict["nCCDL"] = 2
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
            org_dict.get("bankgroup", 1) * org_dict.get("bank", 1),
        )

    @staticmethod
    def _resolve_nRTW(timing_dict, tCK_ps):
        # JESD235D Tables 67 and 68, Note 23.
        tdqsq_max_ps = {1_000: 170, 2_000: 85}.get(timing_dict["rate"])
        if tdqsq_max_ps is None:
            return -1
        tdqsck_max_ps = {
            2_000: 3_500,
            # === Ramulator Guesstimate ===
            # Table 67 (PDF p.116) leaves tDQSCK blank at 1.0 Gb/s; 2.0 Gb/s value used
            1_000: 3_500,
            # =============================
        }[timing_dict["rate"]]
        base_cycles = timing_dict["nCL"] + timing_dict["nBL"] - timing_dict["nCWL"]
        analog_numerator = 3 * tCK_ps + 10 * (tdqsck_max_ps + tdqsq_max_ps)
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
        tRFC_ns = {1024: 110, 2048: 160, 4096: 260}.get(channel_density)
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
        # JESD235D Table 68, Note 29: tREFISB(max) = tREFI / N.
        return 3_900_000 // (num_banks * tCK_ps)


# ---- HBM1 presets ----
# die_density and stack_height: two channels per die, 4-High stack (Table 4 Note 7, PDF p.15, leaves this to the vendor)
HBM1.org_presets = {
    "HBM1_1Gb":  {"channel_density": 1024, "die_density": 2048, "stack_height": 4, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<13, "column": (1<<6) << 1},    # HBM CA already takes BL into account
    "HBM1_2Gb":  {"channel_density": 2048, "die_density": 4096, "stack_height": 4, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 2, "row": 1<<14, "column": (1<<6) << 1},    # HBM CA already takes BL into account
    "HBM1_4Gb":  {"channel_density": 4096, "die_density": 8192, "stack_height": 4, "dq": 128, "channel_width": 128, "bankgroup": 4, "bank": 4, "row": 1<<14, "column": (1<<6) << 1},    # HBM CA already takes BL into account
}

# Organization-dependent and derived timings are resolved below.
HBM1.timing_presets = {
    "HBM1_1Gbps": {
        "rate": 1000, "nBL": 1,
        # === Ramulator Guesstimate ===
        "nCL": 7, "nRCDRD": 7, "nRCDWR": 6,
        "nRP": 7, "nRAS": 17, "nWR": 8,
        "nRTPL": 4, "nCWL": 4,
        "nWTRS": 3, "nWTRL": 4,
        # =============================
        "tCK_ps": 2000,
    },
    "HBM1_2Gbps": {
        "rate": 2000, "nBL": 1,
        # === Ramulator Guesstimate ===
        "nCL": 14, "nRCDRD": 14, "nRCDWR": 12,
        "nRP": 14, "nRAS": 34, "nWR": 16,
        "nRTPL": 5, "nCWL": 5,
        "nWTRS": 6, "nWTRL": 8,
        # =============================
        "tCK_ps": 1000,
    },
}
