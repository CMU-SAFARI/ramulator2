import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class LPDDR6(DRAMStandard):
    name = "LPDDR6"
    # LPDDR6 BL24 physically transfers 12 DQ x 24 beats, with 32 B payload plus
    # metadata. Keep the existing column granularity model but expose the real
    # payload size through data_payload_bytes.
    internal_prefetch_size = 16
    data_payload_bytes = 32
    read_latency = "nRL + nBL_min"

    levels = {
        "Channel":      "N_A",
        "Rank":         "N_A",
        "BankGroup":    "N_A",
        "Bank":         "Closed",
        "Row":          "Closed",
        "Column":       "N_A",
    }

    commands = [
        "ACT1", "ACT2", "PREpb", "PREab",
        "CAS",
        "RD_S", "WR_S", "RDA_S", "WRA_S",
        "RD_L", "WR_L", "RDA_L", "WRA_L",
        "REFab",
    ]

    # LPDDR6 commands use an every-other-CK command protocol.
    command_cycles = {
        "ACT1": 2, "ACT2": 2,
        "PREpb": 2, "PREab": 2,
        "CAS": 2,
        "RD_S": 2, "WR_S": 2, "RDA_S": 2, "WRA_S": 2,
        "RD_L": 2, "WR_L": 2, "RDA_L": 2, "WRA_L": 2,
        "REFab": 2,
    }

    states = ["Opened", "Closed", "Activating", "N_A"]

    # JESD209-6 Table 381: BL/n_min and BL/n_max definitions.
    timing_params = [
        "rate", "nBL_min", "nBL_max", "nBL_min_L", "nBL_max_L", "nRL", "nWL",
        "nACU",
        "nRCDr", "nRCDw", "nRP", "nRPab", "nRAS", "nRC",
        "nWTP", "nRTP", "nRTP_L", "nPPD",
        "nCCDS", "nCCDL", "nCCDL_L", "nCCDS_WR", "nCCDL_WR", "nCCDL_WR_L",
        "nRRD", "nWTRS", "nWTRL", "nRTW_S", "nRTW_L", "nRTW_S_L", "nRTW_L_L",
        "nWCK2DQO", "nRPST", "nODTLon", "nODTon_min",
        "nFAW", "nRFC", "nREFI",
        "nWCKPST", "nCAS", "nAAD", "nCS", "tCK_ps",
    ]

    supported_requests = {
        "Read": "RD_S",
        "Write": "WR_S",
    }

    timing_constraints = [
        # Channel — DQ-bus occupancy (JESD209-6 Table 381 BL/n_min).
        # BL48 is modeled as two transferred BL24 segments.
        TimingConstraint(level="Channel", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nBL_min"),
        TimingConstraint(level="Channel", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min"),
        TimingConstraint(level="Channel", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nBL_min + nBL_min"),
        TimingConstraint(level="Channel", preceding=["WR_L", "WRA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min + nBL_min"),

        # Bank — CAS access gap (JESD209-6 Table 394).
        TimingConstraint(level="Bank", preceding=["CAS"], following=["RD_S", "RDA_S", "RD_L", "RDA_L", "WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nCAS"),

        # Rank — different-BG column timing and turnarounds
        # (JESD209-6 Tables 385 and 390).
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nCCDS"),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nCCDS"),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nCCDS_WR"),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nCCDS_WR"),
        # RD->WR diff-BG = tRTW with BL/n_min (JESD209-6 Table 390).
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRTW_S"),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRTW_S_L"),
        # WR->RD diff-BG = WL + BL/n_min + tWTR_S (JESD209-6 Table 385).
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nWL + nBL_min + nWTRS"),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nWL + nBL_min_L + nWTRS"),

        # Rank switching (sibling) — local bus-clear model: transferred beats + nCS.
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L", "WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L", "WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min + nBL_min + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nRL + nBL_min + nCS - nWL", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nRL + nBL_min + nBL_min + nCS - nWL", window=1, sibling=True),

        # Direct all-bank precharge after column commands
        # (JESD209-6 Tables 383 and 391).
        TimingConstraint(level="Rank", preceding=["RD_S"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["RD_L"], following=["PREab"], latency="nRTP_L"),
        TimingConstraint(level="Rank", preceding=["WR_S"], following=["PREab"], latency="nWL + nBL_max + nWTP"),
        TimingConstraint(level="Rank", preceding=["WR_L"], following=["PREab"], latency="nWL + nBL_max_L + nWTP"),

        # RAS and activation constraints (JESD209-6 Tables 383-385 and 414).
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nRRD"),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT1"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),

        # Rank — all-bank refresh timing (JESD209-6 Tables 302 and 414).
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA_S"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["RDA_L"], following=["REFab"], latency="nRP + nRTP_L"),
        TimingConstraint(level="Rank", preceding=["WRA_S"], following=["REFab"], latency="nWL + nBL_max + nWTP + nRP"),
        TimingConstraint(level="Rank", preceding=["WRA_L"], following=["REFab"], latency="nWL + nBL_max_L + nWTP + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT1", "PREab"], latency="nRFC"),

        # BankGroup — same-BG column timing (JESD209-6 Tables 382-384).
        TimingConstraint(level="BankGroup", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nCCDL_L"),
        TimingConstraint(level="BankGroup", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nCCDL_WR"),
        TimingConstraint(level="BankGroup", preceding=["WR_L", "WRA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nCCDL_WR_L"),

        # BankGroup — same-BG read-to-write (JESD209-6 Table 389).
        TimingConstraint(level="BankGroup", preceding=["RD_S", "RDA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRTW_L"),
        TimingConstraint(level="BankGroup", preceding=["RD_L", "RDA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRTW_L_L"),

        # BankGroup — same-BG write-to-read (JESD209-6 Tables 383-384).
        TimingConstraint(level="BankGroup", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nWL + nBL_max + nWTRL"),
        TimingConstraint(level="BankGroup", preceding=["WR_L", "WRA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nWL + nBL_max_L + nWTRL"),
        TimingConstraint(level="BankGroup", preceding=["ACT1"], following=["ACT1"], latency="nRRD"),

        # Bank — single-bank timing (JESD209-6 Tables 383, 391, and 414).
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["ACT1"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nRCDr"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRCDw"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT1"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD_S"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["RD_L"], following=["PREpb"], latency="nRTP_L"),
        TimingConstraint(level="Bank", preceding=["WR_S"], following=["PREpb"], latency="nWL + nBL_max + nWTP"),
        TimingConstraint(level="Bank", preceding=["WR_L"], following=["PREpb"], latency="nWL + nBL_max_L + nWTP"),
        TimingConstraint(level="Bank", preceding=["RDA_S"], following=["ACT1"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["RDA_L"], following=["ACT1"], latency="nRTP_L + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA_S"], following=["ACT1"], latency="nWL + nBL_max + nWTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA_L"], following=["ACT1"], latency="nWL + nBL_max_L + nWTP + nRP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        cls._resolve_burst_quantities(timing_dict)
        timing_dict["nACU"] = timing_dict.get("nACU", cls._resolve_nACU(tCK_ps))
        timing_dict["nRAS"] = timing_dict.get("nRAS", max(math.ceil(20_000 / tCK_ps), 4))
        timing_dict["nRP"] = timing_dict.get("nRP", timing_dict["nACU"] + max(math.ceil(18_000 / tCK_ps), 4))
        timing_dict["nRPab"] = timing_dict.get("nRPab", timing_dict["nACU"] + max(math.ceil(21_000 / tCK_ps), 4))
        timing_dict["nRC"] = timing_dict.get("nRC", timing_dict["nRAS"] + timing_dict["nRPab"])
        cls._resolve_read_to_write_timings(timing_dict, org_dict)
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["refresh_density_per_2_subchannels"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nACU(tCK_ps):
        # JESD209-6 Tables 414-415: nACU = RU(tACU/tCK), tACU = 22 ns.
        return math.ceil(22_000 / tCK_ps)

    @staticmethod
    def _resolve_burst_quantities(timing_dict):
        # JESD209-6 Tables 381-382: BL48 nBL_min includes the 48-UI transfer
        # plus the mandatory 24-beat segment gap.
        timing_dict["nBL_min"] = timing_dict.get("nBL_min", 6)
        timing_dict["nBL_max"] = timing_dict.get("nBL_max", 12)
        timing_dict["nBL_min_L"] = timing_dict.get("nBL_min_L", 12 + 6)
        timing_dict["nBL_max_L"] = timing_dict.get("nBL_max_L", 24)
        timing_dict["nCCDL_L"] = timing_dict.get("nCCDL_L", 12 + timing_dict["nCCDL"])
        timing_dict["nCCDL_WR_L"] = timing_dict.get("nCCDL_WR_L", 12 + timing_dict["nCCDL_WR"])
        # JESD209-6 Tables 269-272: nRTP(BL48) = nRTP(BL24) +
        # (nBL_min_L - nBL_min) for verified MR-table rows. Derived default;
        # presets should still pin the table value per bin.
        timing_dict["nRTP_L"] = timing_dict.get(
            "nRTP_L", timing_dict["nRTP"] + timing_dict["nBL_min_L"] - timing_dict["nBL_min"]
        )

    @staticmethod
    def _resolve_read_to_write_timings(timing_dict, org_dict):
        # Read-to-write turnaround (tRTW), JESD209-6 Tables 389
        # (same-BG: BL/n_max) and 390 (different-BG: BL/n_min), evaluated per
        # preceding-burst length.
        odt_enabled = org_dict.get("odt_enabled", True)
        n_wck2dqo = timing_dict["nWCK2DQO"]
        if odt_enabled:
            common = n_wck2dqo + timing_dict["nRPST"] - timing_dict["nODTLon"] - timing_dict["nODTon_min"] + 1
        else:
            common = n_wck2dqo - timing_dict["nWL"]

        nRL = timing_dict["nRL"]
        timing_dict["nRTW_S"] = timing_dict.get("nRTW_S", nRL + timing_dict["nBL_min"] + common)
        timing_dict["nRTW_L"] = timing_dict.get("nRTW_L", nRL + timing_dict["nBL_max"] + common)
        timing_dict["nRTW_S_L"] = timing_dict.get("nRTW_S_L", nRL + timing_dict["nBL_min_L"] + common)
        timing_dict["nRTW_L_L"] = timing_dict.get("nRTW_L_L", nRL + timing_dict["nBL_max_L"] + common)

    @staticmethod
    def _resolve_nRFC(refresh_density_per_2_subchannels, tCK_ps):
        # JESD209-6 Table 302 defines density per 2 sub-channels.
        if refresh_density_per_2_subchannels <= 4096:
            raise ValueError("LPDDR6: JESD209-6 Table 302 tRFCab is TBD for 4Gb per 2 sub-channels")
        elif refresh_density_per_2_subchannels <= 8192:
            tRFC_ns = 210
        elif refresh_density_per_2_subchannels <= 16384:
            tRFC_ns = 280
        elif refresh_density_per_2_subchannels <= 32768:
            tRFC_ns = 380
        else:
            raise ValueError("LPDDR6: JESD209-6 Table 302 tRFCab is TBD above 32Gb per 2 sub-channels")
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # JESD209-6 Table 302: tREFI = 3906 ns.
        return math.ceil(3_906_000 / tCK_ps)


LPDDR6.org_presets = {
    "LPDDR6_16Gb_x12": {
        "density": 16384,
        # JESD209-6 Table 302 density is per 2 sub-channels: 2 * 16Gb = 32Gb.
        "refresh_density_per_2_subchannels": 32768,
        "dq": 12,
        "channel_width": 12,
        "odt_enabled": True,
        "rank": 1,
        "bankgroup": 4,
        "bank": 4,
        "row": 1 << 16,
        "column": 1 << 10,
    },
}


LPDDR6.timing_presets = {
    "LPDDR6_10667_BL24": {
        "rate": 10667,
        # JESD209-6 Tables 381-382: nBL_min = BL/n_min(BL24);
        # nBL_max / nBL_min_L / nBL_max_L / nCCDL_L / nCCDL_WR_L are derived per
        # the selected burst timing in resolve_secondary_timings.
        "nBL_min": 6,
        "nRL": 56,
        "nWL": 26,
        "nRCDr": 48,
        "nRCDw": 22,
        "nWTP": 32,
        "nRTP": 14,
        # JESD209-6 Tables 269-272: nRTP(BL48) column at the RL=56 row.
        "nRTP_L": 26,
        "nPPD": 4,
        "nCCDS": 6,
        "nCCDL": 10,
        "nCCDS_WR": 6,
        "nCCDL_WR": 10,
        "nRRD": 10,
        "nWTRS": 17,
        "nWTRL": 32,
        # ODT-on read-to-write model. These values preserve the current 10667
        # BL24 timing while making the ODT assumption explicit.
        "nWCK2DQO": 5,
        "nRPST": 0,
        "nODTLon": 27,
        "nODTon_min": 0,
        "nFAW": 40,
        "nWCKPST": 3,
        "nCAS": 2,
        "nAAD": 8,
        "nCS": 2,
        "tCK_ps": 375,
    },
}
