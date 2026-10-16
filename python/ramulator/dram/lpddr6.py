import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint

# Values of the "wck_mode" organization field; see LPDDR6._resolve_wck_mode.
WCK_MODES = ("sync", "always_on")


class LPDDR6(DRAMStandard):
    name = "LPDDR6"
    # LPDDR6 BL24 physically transfers 12 DQ x 24 beats, with 32 B payload plus
    # metadata. Keep the existing column granularity model but expose the real
    # payload size through data_payload_bytes.
    internal_prefetch_size = 16
    data_payload_bytes = 32
    # RL counts from the command's second CK edge, then tWCK2DQO (JESD209-6 §7.4.2.2, Table 477).
    read_latency = "1 + nRL + nWCK2DQO + nBL_min"

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
    # Even nCK command gaps (JESD209-6 Tables 383-385 Note 2).
    command_slot_cycles = 2
    config_modes = ["wck_mode"]

    states = ["Opened", "Closed", "Activating", "N_A"]

    # JESD209-6 Table 381: BL/n_min and BL/n_max definitions.
    timing_params = [
        "rate", "nBL_min", "nBL_max", "nBL_min_L", "nBL_max_L", "nRL", "nWL",
        "nACU",
        "nRCDr", "nRCDw", "nRP", "nRPab", "nRAS", "nRC",
        "nWTP", "nRTP", "nRTP_L", "nPPD",
        "nCCDS", "nCCDL", "nCCDL_L", "nCCDS_WR", "nCCDL_WR", "nCCDL_WR_L",
        "nRRD",
        "nWTRS", "nWTRL",
        "nRTW_S", "nRTW_L", "nRTW_S_L", "nRTW_L_L",
        "nWCK2DQO", "nWCK2DQO_r2r", "nRPST", "nRPRE",
        "nODTLon", "nODTLoff", "nODTLoff_L", "nODTon_min", "nODToff_max",
        "nR2R_RD_RD", "nR2R_RDL_RD", "nR2R_RD_WR", "nR2R_RDL_WR",
        "nR2R_WR_RD", "nR2R_WRL_RD", "nR2R_WR_WR", "nR2R_WRL_WR",
        "nFAW", "nRFC", "nREFI",
        "nWCKPST", "nWCKENL_RD", "nWCKENL_WR", "nCAS", "nAAD", "tCK_ps",
    ]

    supported_requests = {"Read": "RD_S", "Write": "WR_S"}

    timing_constraints = [
        # Channel — DQ-bus occupancy, BL24 and BL48 BL/n_min (JESD209-6 Table 381).
        TimingConstraint(level="Channel", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nBL_min"),
        TimingConstraint(level="Channel", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min"),
        TimingConstraint(level="Channel", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nBL_min_L"),
        TimingConstraint(level="Channel", preceding=["WR_L", "WRA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nBL_min_L"),

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

        # Rank — rank to rank, DQ ODT on (JESD209-6 Tables 401 and 404, selected by wck_mode).
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nR2R_RD_RD", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nR2R_RDL_RD", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nR2R_RD_WR", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nR2R_RDL_WR", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nR2R_WR_RD", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nR2R_WRL_RD", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nR2R_WR_WR", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nR2R_WRL_WR", window=1, sibling=True),

        # Rank — column command to PREab, with or without auto-precharge (JESD209-6 Tables 383 and 391-393).
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["RD_L", "RDA_L"], following=["PREab"], latency="nRTP_L"),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["PREab"], latency="nWL + nBL_max + nWTP"),
        TimingConstraint(level="Rank", preceding=["WR_L", "WRA_L"], following=["PREab"], latency="nWL + nBL_max_L + nWTP"),

        # RAS and activation constraints (JESD209-6 Figures 67-72 and Tables 383-385, 414).
        TimingConstraint(level="Rank", preceding=["ACT2"], following=["ACT2"], latency="nRRD"),
        TimingConstraint(level="Rank", preceding=["ACT2"], following=["ACT2"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT2"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT2"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),

        # Rank — all-bank refresh timing (JESD209-6 Figure 111 and Tables 300-302, 414).
        TimingConstraint(level="Rank", preceding=["ACT2"], following=["REFab"], latency="nRAS + nRP"),
        TimingConstraint(level="Rank", preceding=["PREpb"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["REFab"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["RDA_S"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["RDA_L"], following=["REFab"], latency="nRP + nRTP_L"),
        TimingConstraint(level="Rank", preceding=["WRA_S"], following=["REFab"], latency="nWL + nBL_max + nWTP + nRP"),
        TimingConstraint(level="Rank", preceding=["WRA_L"], following=["REFab"], latency="nWL + nBL_max_L + nWTP + nRP"),
        # tRFCab ends at ACT-2 and blocks PRE/REF (JESD209-6 Table 300, Figure 111 Notes 1 and 3).
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT2"], latency="nRFC"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["PREab", "PREpb", "REFab"], latency="nRFC"),

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

        # BankGroup — ACT-2 to ACT-2 timing (JESD209-6 Figure 71).
        TimingConstraint(level="BankGroup", preceding=["ACT2"], following=["ACT2"], latency="nRRD"),

        # Bank — single-bank timing (JESD209-6 Figures 67-70 and Tables 383, 391, 414).
        TimingConstraint(level="Bank", preceding=["ACT2"], following=["ACT2"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT2"], following=["RD_S", "RDA_S", "RD_L", "RDA_L"], latency="nRCDr"),
        TimingConstraint(level="Bank", preceding=["ACT2"], following=["WR_S", "WRA_S", "WR_L", "WRA_L"], latency="nRCDw"),
        TimingConstraint(level="Bank", preceding=["ACT2"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT2"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD_S"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["RD_L"], following=["PREpb"], latency="nRTP_L"),
        TimingConstraint(level="Bank", preceding=["WR_S"], following=["PREpb"], latency="nWL + nBL_max + nWTP"),
        TimingConstraint(level="Bank", preceding=["WR_L"], following=["PREpb"], latency="nWL + nBL_max_L + nWTP"),
        TimingConstraint(level="Bank", preceding=["RDA_S"], following=["ACT2"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["RDA_L"], following=["ACT2"], latency="nRTP_L + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA_S"], following=["ACT2"], latency="nWL + nBL_max + nWTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA_L"], following=["ACT2"], latency="nWL + nBL_max_L + nWTP + nRP"),
    ]

    @classmethod
    def validate_organization(cls, org_dict):
        super().validate_organization(org_dict)
        mode = org_dict["wck_mode"]
        if mode not in WCK_MODES:
            raise ValueError(f"{cls.name}: wck_mode must be one of {WCK_MODES}, got {mode!r}")

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        rate = timing_dict["rate"]
        tCK_ps = timing_dict["tCK_ps"]
        timing_dict["nBL_max"] = cls._resolve_nBL_max(rate)
        timing_dict["nBL_min_L"] = cls._resolve_nBL_min_L(rate)
        timing_dict["nBL_max_L"] = cls._resolve_nBL_max_L(rate)
        timing_dict["nCCDL_L"] = cls._resolve_nCCDL_L(
            rate, timing_dict["nCCDL"]
        )
        timing_dict["nCCDL_WR_L"] = cls._resolve_nCCDL_WR_L(
            rate, timing_dict["nCCDL_WR"]
        )
        timing_dict["nACU"] = cls._resolve_nACU(rate)
        timing_dict["nRAS"] = cls._resolve_nRAS(tCK_ps)
        timing_dict["nRP"] = cls._resolve_nRP(timing_dict["nACU"], tCK_ps)
        timing_dict["nRPab"] = cls._resolve_nRPab(timing_dict["nACU"], tCK_ps)
        timing_dict["nRC"] = cls._resolve_nRC(
            timing_dict["nRAS"], timing_dict["nRP"]
        )
        cls._resolve_wck_mode(timing_dict, org_dict["wck_mode"], org_dict["odt_enabled"])

        nRL = timing_dict["nRL"]
        nWCK2DQO = timing_dict["nWCK2DQO"]
        nRPST = timing_dict["nRPST"]
        nODTLon = timing_dict["nODTLon"]
        nODTon_min = timing_dict["nODTon_min"]
        nWL = timing_dict["nWL"]
        odt_enabled = org_dict["odt_enabled"]
        timing_dict["nRTW_S"] = cls._resolve_nRTW_S(
            timing_dict["nBL_min"],
            nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled,
        )
        timing_dict["nRTW_L"] = cls._resolve_nRTW_L(
            timing_dict["nBL_max"],
            nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled,
        )
        timing_dict["nRTW_S_L"] = cls._resolve_nRTW_S_L(
            timing_dict["nBL_min_L"],
            nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled,
        )
        timing_dict["nRTW_L_L"] = cls._resolve_nRTW_L_L(
            timing_dict["nBL_max_L"],
            nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled,
        )
        # JESD209-6 Table 2 Note 1: die density is twice sub-channel density.
        timing_dict["nRFC"] = cls._resolve_nRFC(
            2 * org_dict["subchannel_density"], tCK_ps
        )
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)

    # Rank-to-rank gaps (JESD209-6 §8.9); "always_on" is WCK always-on mode (MR22 OP[5], Table 117).
    # RU((tWCKPST - 0.5tWCK)/tCK) = nWCKPST (Table 117); RU((tRPST [+ tRPRE] - 0.5tWCK)/tCK) = nRPST [+ nRPRE] (Table 90, 1:1 WCK:RDQS).
    @staticmethod
    def _resolve_wck_mode(timing_dict, mode, odt_enabled):
        t = timing_dict
        for burst, nBL, nODTLoff in (("", t["nBL_min"], t["nODTLoff"]), ("L", t["nBL_min_L"], t["nODTLoff_L"])):
            if nBL == -1 or not isinstance(odt_enabled, bool):
                rd_rd = rd_wr = wr_rd = wr_wr = -1
            elif mode == "sync" and odt_enabled:
                # Table 404: DQ ODT on, WCK always-on off.
                rd_rd = t["nRL"] + nBL + t["nWCKPST"] - t["nWCKENL_RD"]
                rd_wr = t["nRL"] + nBL + t["nWCKPST"] - min(t["nODTLon"] + t["nODTon_min"], t["nWCKENL_WR"])
                wr_rd = t["nWL"] + nBL + max(t["nODToff_max"], t["nWCKPST"]) - t["nWCKENL_RD"]
                wr_wr = t["nWL"] + nBL + max(t["nODToff_max"], t["nWCKPST"]) - t["nWCKENL_WR"]
            elif mode == "sync":
                # Table 405: DQ ODT off, WCK always-on off.
                rd_rd = t["nRL"] + nBL + t["nWCKPST"] - t["nWCKENL_RD"]
                rd_wr = t["nRL"] + nBL + t["nWCKPST"] - t["nWCKENL_WR"]
                wr_rd = t["nWL"] + nBL + t["nWCKPST"] - t["nWCKENL_RD"]
                wr_wr = t["nWL"] + nBL + t["nWCKPST"] - t["nWCKENL_WR"]
            elif odt_enabled:
                # Table 401: DQ ODT on, WCK always-on on.
                rd_rd = nBL + t["nWCK2DQO_r2r"] + t["nRPST"] + t["nRPRE"]
                rd_wr = t["nRL"] + nBL + t["nWCK2DQO"] - t["nODTLon"] - t["nODTon_min"] + 1
                wr_rd = nODTLoff + t["nODToff_max"] - t["nRL"]
                wr_wr = nODTLoff + t["nODToff_max"] - t["nODTLon"]
            else:
                # Table 403: DQ ODT off, WCK always-on on; Notes 3-4: RU(tWCK2DQI[_rank2rank](max)/tCK) = 1.
                rd_rd = nBL + t["nWCK2DQO_r2r"] + t["nRPST"] + t["nRPRE"]
                rd_wr = t["nRL"] + nBL + t["nWCK2DQO"] + t["nRPST"] - t["nWL"]
                wr_rd = t["nWL"] + nBL + 1 - t["nRL"]
                wr_wr = nBL + 1
            t[f"nR2R_RD{burst}_RD"] = rd_rd
            t[f"nR2R_RD{burst}_WR"] = rd_wr
            t[f"nR2R_WR{burst}_RD"] = wr_rd
            t[f"nR2R_WR{burst}_WR"] = wr_wr

    @staticmethod
    def _lookup_speed_grade(rate, table):
        # LPDDR6 speed-grade rows use lower-exclusive, upper-inclusive bounds.
        for (lower_rate, upper_rate), value in table.items():
            if lower_rate < rate <= upper_rate:
                return value
        return -1

    @classmethod
    def _resolve_nACU(cls, rate):
        # JESD209-6 Tables 414-415: nACU for each defined speed-grade range.
        return cls._lookup_speed_grade(rate, {
            (80, 1067): 6,
            (1067, 1600): 9,
            (1600, 2133): 12,
            (2133, 2750): 16,
            (2750, 3200): 18,
            (3200, 3750): 21,
            (3750, 4267): 24,
            (4267, 4800): 27,
            (4800, 5500): 31,
            (5500, 6400): 36,
            (6400, 7500): 42,
            (7500, 8533): 47,
            (8533, 9600): 53,
            (9600, 10667): 59,
        })

    @staticmethod
    def _resolve_nRAS(tCK_ps):
        return max(math.ceil(20_000 / tCK_ps), 4)

    @staticmethod
    def _resolve_nRP(nACU, tCK_ps):
        if nACU == -1:
            return -1
        return nACU + max(math.ceil(18_000 / tCK_ps), 4)

    @staticmethod
    def _resolve_nRPab(nACU, tCK_ps):
        if nACU == -1:
            return -1
        return nACU + max(math.ceil(21_000 / tCK_ps), 4)

    @staticmethod
    def _resolve_nRC(nRAS, nRP):
        if nRAS == -1 or nRP == -1:
            return -1
        return nRAS + nRP

    # JESD209-6 Table 381: burst timing splits at WCK 3200 MHz = 6400 Mb/s.
    @staticmethod
    def _resolve_nBL_max(rate):
        if rate <= 80:
            return -1
        return 6 if rate <= 6400 else 12

    @staticmethod
    def _resolve_nBL_min_L(rate):
        if rate <= 80:
            return -1
        return 12 if rate <= 6400 else 18

    @staticmethod
    def _resolve_nBL_max_L(rate):
        if rate <= 80:
            return -1
        return 12 if rate <= 6400 else 24

    @staticmethod
    def _resolve_nCCDL_L(rate, nCCDL):
        if rate <= 80:
            return -1
        return 12 if rate <= 6400 else 12 + nCCDL

    @staticmethod
    def _resolve_nCCDL_WR_L(rate, nCCDL_WR):
        if rate <= 80:
            return -1
        return 12 if rate <= 6400 else 12 + nCCDL_WR

    # JESD209-6 Tables 389-390: read-to-write gaps are rounded to even nCK.
    @staticmethod
    def _resolve_nRTW_S(
        nBL_min, nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled
    ):
        if nBL_min == -1:
            return -1
        if odt_enabled is True:
            value = nRL + nBL_min + nWCK2DQO + nRPST - nODTLon - nODTon_min + 1
        elif odt_enabled is False:
            value = nRL + nBL_min + nWCK2DQO - nWL
        else:
            return -1
        return value + value % 2

    @staticmethod
    def _resolve_nRTW_L(
        nBL_max, nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled
    ):
        if nBL_max == -1:
            return -1
        if odt_enabled is True:
            value = nRL + nBL_max + nWCK2DQO + nRPST - nODTLon - nODTon_min + 1
        elif odt_enabled is False:
            value = nRL + nBL_max + nWCK2DQO - nWL
        else:
            return -1
        return value + value % 2

    @staticmethod
    def _resolve_nRTW_S_L(
        nBL_min_L, nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled
    ):
        if nBL_min_L == -1:
            return -1
        if odt_enabled is True:
            value = nRL + nBL_min_L + nWCK2DQO + nRPST - nODTLon - nODTon_min + 1
        elif odt_enabled is False:
            value = nRL + nBL_min_L + nWCK2DQO - nWL
        else:
            return -1
        return value + value % 2

    @staticmethod
    def _resolve_nRTW_L_L(
        nBL_max_L, nRL, nWCK2DQO, nRPST, nODTLon, nODTon_min, nWL, odt_enabled
    ):
        if nBL_max_L == -1:
            return -1
        if odt_enabled is True:
            value = nRL + nBL_max_L + nWCK2DQO + nRPST - nODTLon - nODTon_min + 1
        elif odt_enabled is False:
            value = nRL + nBL_max_L + nWCK2DQO - nWL
        else:
            return -1
        return value + value % 2

    @staticmethod
    def _resolve_nRFC(die_density, tCK_ps):
        # JESD209-6 Table 302 defines density per 2 sub-channels.
        tRFC_ns = {
            4096: None,
            6144: 210,
            8192: 210,
            12288: 280,
            16384: 280,
            24576: 380,
            32768: 380,
            49152: None,
            65536: None,
        }.get(die_density)
        if tRFC_ns is None:
            return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # JESD209-6 Table 302: tREFI = 3906 ns maximum average interval.
        return 3_906_000 // tCK_ps


LPDDR6.org_presets = {
    "LPDDR6_16Gb_x12": {
        # JESD209-6 Table 2: one modeled x12 sub-channel.
        "subchannel_density": 16384,
        "dq": 12,
        "channel_width": 12,
        "odt_enabled": True,
        "wck_mode": "sync",
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
        "nRL": 56, "nWL": 26,
        "nRCDr": 48, "nRCDw": 22,
        "nWTP": 32, "nRTP": 14,
        # JESD209-6 Tables 269-272: nRTP(BL48) column at the RL=56 row.
        "nRTP_L": 26,
        "nPPD": 4,
        "nCCDS": 6, "nCCDL": 10, "nCCDS_WR": 6, "nCCDL_WR": 10,
        "nRRD": 10, "nWTRS": 17, "nWTRL": 32,
        # JESD209-6 Tables 319-320 and 477: ODT-enabled, high-frequency mode.
        "nWCK2DQO": 5,
        "nODTLon": 16, "nODTon_min": 4, "nODToff_max": 10,
        # JESD209-6 Table 319: ODTLoff = WL + BL/n_min + RU(tWCK2DQI_HF(max)/tCK) for BL24 and BL48 (Table 477).
        "nODTLoff": 34, "nODTLoff_L": 46,
        # === Ramulator Guesstimate ===
        # RU(tWCK2DQO_rank2rank(max)/tCK): JESD209-6 §9.3 is TBD; JESD209-5C Table 338 HF max 400 ps.
        "nWCK2DQO_r2r": 2,
        # =============================
        "nFAW": 40,
        # JESD209-6 Tables 256 and 260: tWCKENL_RD at RL Set 0, tWCKENL_WR at WL Set A.
        "nWCKENL_RD": 32, "nWCKENL_WR": 10,
        # JESD209-6 §7.3.3.5 and Table 117: RD(tWCKPST/tCK) at the MR22 default 2.5 tWCK.
        "nWCKPST": 1,
        "nCAS": 2,
        # JESD209-6 Table 90: RD(tRPST/tCK) and RU(tRPRE/tCK) at the MR10 defaults 0.5 tWCK and 4 tWCK.
        "nRPST": 0, "nRPRE": 2,
        # JESD209-6 §7.4.1: tAAD = 8 nCK.
        "nAAD": 8,
        "tCK_ps": 375,
    },
}
