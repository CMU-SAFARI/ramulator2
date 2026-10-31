import warnings

from ramulator.dram.spec import DRAMStandard, JEDECWarning, TimingConstraint

# Values of the "prac" organization field (Per Row Activation Counting, JESD79-5C §16,
# MR70:OP[1]); see DDR5._resolve_prac.
PRAC_MODES = ("off", "on")


class DDR5(DRAMStandard):
    name = "DDR5"
    internal_prefetch_size = 16
    # JESD79-5C Fig. 35: RL is measured from the 2nd cycle of the 2-cycle RD;
    # commands are timestamped at their 1st cycle.
    read_latency = "nCL + nBL + 1"
    # PRAC is fixed at initialization (JESD79-5C §16.1); the PRAC controller warns without "on".
    config_modes = ["prac"]

    # ---- Hierarchy (level name → init state) ----
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
        "ACT", "PREpb", "PREab",
        "RD", "WR", "RDA", "WRA",
        "REFab", "PREsb", "REFsb", "RFMab", "RFMsb",
    ]
    # Automatic refresh uses normal-mode REFab; REFsb and RFMsb are not
    # scheduled by the default refresh path.

    # ---- CA bus cycle count per command (JEDEC Table 30) ----
    # 2-cycle commands carry address bits that don't fit in 1 CA cycle.
    # The DRAM only begins internal operation after the 2nd cycle.
    # Multi-cycle adjustment applied automatically in to_config().
    command_cycles = {"ACT": 2, "RD": 2, "RDA": 2, "WR": 2, "WRA": 2}

    # ---- States ----
    states = ["Opened", "Closed", "N_A"]

    # ---- Timing parameters (C++ Timing enum order) ----
    timing_params = [
        "rate", "nBL", "nCL", "nRCD", "nRP", "nRAS", "nRC",
        "nWR", "nRTP", "nCWL", "nPPD",
        "nCCDS", "nCCDL", "nCCDS_WR", "nCCDL_WR",
        "nCCDM", "nCCDM_WR", "nWTRM",  # Sibling bank timings for high speedbins
        "nRTW",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL",
        "nFAW", "nRFC", "nREFI", "nRFCsb",
        "nCS", "tCK_ps",
        "nRFM", "nRFMsb", "nREFSBRD",
        "nRPab",
    ]

    # ---- External request types ----
    supported_requests = {"Read": "RD", "Write": "WR"}

    # ---- Timing constraints ----
    timing_constraints = [
        # Channel — data bus occupancy
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Rank — CAS read timing (different bank group)
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        # Rank — CAS write timing (different bank group, DDR5 separates read/write)
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS_WR"),
        # Rank — read-to-write turnaround
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        # Rank — write-to-read turnaround
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        # Rank — sibling (rank switching): nCS data-bus gap between ranks in each direction.
        # RD→WR to another rank still needs the same postamble/preamble clearance as the
        # same-rank tCCD_x_RTW (JESD79-5C Tables 330-334), so it is nRTW plus the rank gap.
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nCS - nCL", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL + nCS", window=1, sibling=True),
        # Rank — CAS to PREab
        TimingConstraint(level="Rank", preceding=["RD", "RDA"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        # Rank — RAS timing
        TimingConstraint(level="Rank", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Rank", preceding=["ACT", "REFsb", "RFMsb"], following=["ACT", "REFsb", "RFMsb"], latency="nFAW", window=4, shared_window=True),
        TimingConstraint(level="Rank", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT"], latency="nRPab"),
        # Rank — precharge-to-precharge delay (DDR5-specific nPPD)
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREsb"], latency="nPPD"),
        TimingConstraint(level="Rank", preceding=["PREsb"], following=["PREpb", "PREab", "PREsb"], latency="nPPD"),
        # Rank — RAS to REF
        TimingConstraint(level="Rank", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREsb"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["REFab"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        # JESD79-5C Table 68: tRFC1 between REFab commands; §4.13: only DES/NOP until tRFC.
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT", "PREpb", "PREab", "REFab"], latency="nRFC"),

        # Rank — RFMab timing
        TimingConstraint(level="Rank", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREsb"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["RFMab"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["PREsb", "REFsb", "RFMab", "RFMsb"], latency="nRFC"),
        TimingConstraint(level="Rank", preceding=["RFMab"], following=["ACT", "PREpb", "PREab", "PREsb", "REFab", "REFsb", "RFMab", "RFMsb"], latency="nRFM"),

        TimingConstraint(level="Rank", preceding=["REFsb"], following=["REFab", "REFsb", "RFMab", "RFMsb"], latency="nRFCsb"),
        TimingConstraint(level="Rank", preceding=["RFMsb"], following=["REFab", "REFsb", "RFMab", "RFMsb"], latency="nRFMsb"),

        # Bank — same-bank read CAS timing
        TimingConstraint(level="Bank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        # Bank — same-bank write CAS timing
        TimingConstraint(level="Bank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL_WR"),
        # Bank — same-bank write-to-read
        TimingConstraint(level="Bank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        # Bank — JESD79-5C Tables 330-334: Write to Read with Auto Precharge, same bank: tCCD_WTRA = CWL + WBL/2 + tWR - tRTP
        TimingConstraint(level="Bank", preceding=["WR"], following=["RDA"], latency="nCWL + nBL + nWR - nRTP"),

        # Sibling bank timings for high speedbins
        TimingConstraint(level="Bank", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDM", sibling=True),
        TimingConstraint(level="Bank", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDM_WR", sibling=True),
        TimingConstraint(level="Bank", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRM", sibling=True),

        # BankGroup — same-group RAS timing
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),

        # Bank — single-bank timing
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA", "WR", "WRA"], latency="nRCD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),

        # Bank — PREsb targets one bank address across every bank group.
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREsb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREsb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREsb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["PREsb"], following=["ACT"], latency="nRP"),

        # Bank — REFsb targets one bank address across every bank group.
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFsb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFsb"], latency="nRRDL", sibling=True),
        TimingConstraint(level="Bank", preceding=["PREpb", "PREsb"], following=["REFsb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["PREab"], following=["REFsb"], latency="nRPab"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["REFsb"], latency="nRP + nRTP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["REFsb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["REFsb"], following=["ACT", "PREpb", "PREab", "PREsb"], latency="nRFCsb"),
        TimingConstraint(level="Bank", preceding=["REFsb"], following=["ACT"], latency="nREFSBRD", sibling=True),

        # Bank — RFMsb targets one bank address across every bank group.
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMsb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMsb"], latency="nRRDL", sibling=True),
        TimingConstraint(level="Bank", preceding=["PREpb", "PREsb"], following=["RFMsb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["PREab"], following=["RFMsb"], latency="nRPab"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["RFMsb"], latency="nRP + nRTP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["RFMsb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["RFMsb"], following=["ACT", "PREpb", "PREab", "PREsb"], latency="nRFMsb"),
        TimingConstraint(level="Bank", preceding=["RFMsb"], following=["ACT"], latency="nREFSBRD", sibling=True),
    ]

    @classmethod
    def validate_organization(cls, org_dict):
        super().validate_organization(org_dict)
        prac = org_dict["prac"]
        if prac not in PRAC_MODES:
            raise ValueError(f"{cls.name}: prac must be one of {PRAC_MODES}, got {prac!r}")
        # JESD79-5C §16.1: PRAC is optional on 16Gb and higher density devices only.
        if prac == "on" and org_dict["density"] < 16384:
            warnings.warn(
                f"{cls.name}: prac='on' on a {org_dict['density']} Mb device; JESD79-5C §16.1 "
                f"defines PRAC for 16Gb and larger devices only",
                JEDECWarning,
                stacklevel=2,
            )

    # ---- Secondary timing resolution ----
    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        dq = org_dict["dq"]
        if dq in (4, 8, 16):
            page_size_bits = org_dict["column"] * dq
            page_size_bytes = page_size_bits // 8 if page_size_bits % 8 == 0 else -1
        else:
            page_size_bytes = -1
        timing_dict["nRRDS"] = 8 if page_size_bytes in (1024, 2048) else -1
        timing_dict["nRRDL"] = cls._resolve_nRRDL(
            page_size_bytes, timing_dict["rate"], timing_dict["tCK_ps"]
        )
        timing_dict["nFAW"] = cls._resolve_nFAW(page_size_bytes)
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["density"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])
        timing_dict["nRFCsb"] = cls._resolve_nRFCsb(
            org_dict["density"], timing_dict["tCK_ps"]
        )
        # JESD79-5C Table 169: tRFM1 = tRFC1 and tRFMsb = tRFCsb.
        timing_dict["nRFM"] = timing_dict["nRFC"]
        timing_dict["nRFMsb"] = timing_dict["nRFCsb"]
        timing_dict["nREFSBRD"] = cls._resolve_nREFSBRD(
            org_dict["density"], timing_dict["tCK_ps"]
        )
        cls._resolve_prac(timing_dict, org_dict["prac"])

        rate = timing_dict["rate"]
        tCK_ps = timing_dict["tCK_ps"]

        timing_dict["nCCDL_WR"] = cls._resolve_nCCDL_WR(dq, tCK_ps)
        timing_dict["nCCDM"] = cls._resolve_nCCDM(rate, tCK_ps, timing_dict["nCCDL"])
        # JESD79-5C Tables 48/51/53, 330-334: x8/x16 writes are JW, so the whole bank group
        # uses tCCD_L_WR2 (= nCCDL_WR); the RMW-sized tCCD_M_WR applies to x4.
        if dq in (8, 16):
            timing_dict["nCCDM_WR"] = timing_dict["nCCDL_WR"]
        else:
            timing_dict["nCCDM_WR"] = cls._resolve_nCCDM_WR(rate, tCK_ps)
        timing_dict["nWTRM"] = cls._resolve_nWTRM(rate, tCK_ps, timing_dict["nWTRL"])
        timing_dict["nRTW"] = cls._resolve_nRTW(
            rate,
            timing_dict["nCL"],
            timing_dict["nBL"],
            timing_dict["nCWL"],
        )

    @staticmethod
    def _resolve_nRRDL(page_size_bytes, rate, tCK_ps):
        if page_size_bytes not in (1024, 2048):
            return -1
        tRRDL_ps = {
            3200: 5_000,
            3600: 5_000,
            4000: 5_000,
            4400: 5_000,
            4800: 5_000,
            5200: 5_000,
            5600: 5_000,
            6000: 5_000,
            6400: 5_000,
            6800: 4_705,
            7200: 4_444,
            7600: 4_210,
            8000: 4_000,
            8400: 4_000,
            8800: 3_863,
        }.get(rate)
        if tRRDL_ps is None:
            return -1
        return max(8, DDR5._min_cycles(tRRDL_ps, tCK_ps))

    @staticmethod
    def _resolve_nFAW(page_size_bytes):
        return {1024: 32, 2048: 40}.get(page_size_bytes, -1)

    @classmethod
    def _resolve_nCCDL_WR(cls, dq, tCK_ps):
        timing = {
            4: (32, 20_000),  # rmw_write_gap
            8: (16, 10_000),  # jw_write_gap
            16: (16, 10_000),  # jw_write_gap
        }.get(dq)
        if timing is None:
            return -1
        nCK, timing_ps = timing
        return max(nCK, cls._min_cycles(timing_ps, tCK_ps))

    @staticmethod
    def _resolve_nRFC(density, tCK_ps):
        tRFC_ps = {
            8192: 195_000,
            16384: 295_000,
            24576: 410_000,
            32768: 410_000,
        }.get(density)
        if tRFC_ps is None:
            return -1
        return DDR5._min_cycles(tRFC_ps, tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return 3_900_000 // tCK_ps

    @classmethod
    def _resolve_nRFCsb(cls, density, tCK_ps):
        tRFCsb_ps = {
            8192: 115_000,
            16384: 130_000,
            24576: 190_000,
            32768: 190_000,
        }.get(density)
        if tRFCsb_ps is None:
            return -1
        return cls._min_cycles(tRFCsb_ps, tCK_ps)

    @classmethod
    def _resolve_nREFSBRD(cls, density, tCK_ps):
        tREFSBRD_ps = {
            8192: 30_000,
            16384: 30_000,
            24576: 30_000,
            32768: 30_000,
        }.get(density)
        if tREFSBRD_ps is None:
            return -1
        return cls._min_cycles(tREFSBRD_ps, tCK_ps)

    @staticmethod
    def _min_cycles(timing_ps, tCK_ps):
        """Apply the JESD79-5C Section 13.2 minimum-timing conversion."""
        return ((timing_ps * 997) // tCK_ps + 1000) // 1000

    # JESD79-5C §16.3 Table 343: with PRAC on, the activation counter update sets
    # tRAS 16, tRP 36, tRC 52, tRTP 5 and tWR 10 ns; nRTP and nWR take the PRAC
    # settings of MR6. The speed-bin tables carry no PRAC column, so nRP is rounded
    # from 36 ns. Table 344: a PREab closing 9 to 32 rows needs tRP,min + 4 ns; the
    # timing graph does not count open rows, so every PREab uses it. Tables 346/347:
    # tRFMab = 5 * tRRFab (70 ns), tRFMsb = 5 * tRRFsb (60 ns). tRFC is unchanged (§16.4.1).
    @classmethod
    def _resolve_prac(cls, timing_dict, prac):
        if prac == "off":
            timing_dict["nRPab"] = timing_dict["nRP"]
            return
        tCK_ps = timing_dict["tCK_ps"]
        timing_dict["nRAS"] = cls._min_cycles(16_000, tCK_ps)
        timing_dict["nRP"] = cls._min_cycles(36_000, tCK_ps)
        timing_dict["nRC"] = max(cls._min_cycles(52_000, tCK_ps), timing_dict["nRAS"] + timing_dict["nRP"])
        timing_dict["nRTP"] = cls._mr6_setting(cls._min_cycles(5_000, tCK_ps), range(8, 23))
        timing_dict["nWR"] = cls._mr6_setting(cls._min_cycles(10_000, tCK_ps), range(16, 45, 2))
        timing_dict["nRPab"] = cls._min_cycles(36_000 + 4_000, tCK_ps)
        timing_dict["nRFM"] = cls._min_cycles(5 * 70_000, tCK_ps)
        timing_dict["nRFMsb"] = cls._min_cycles(5 * 60_000, tCK_ps)

    @staticmethod
    def _mr6_setting(nCK, settings):
        # MR6 Notes 1/2: the smallest setting covering the minimum; -1 if none does.
        return next((setting for setting in settings if setting >= nCK), -1)

    # JESD79-5C Tables 330-334: M timings.
    @classmethod
    def _resolve_nCCDM(cls, rate, tCK_ps, nCCDL):
        nCCDM = {
            3200: nCCDL,
            3600: nCCDL,
            4000: nCCDL,
            4400: nCCDL,
            4800: nCCDL,
            5200: nCCDL,
            5600: nCCDL,
            6000: nCCDL,
            6400: nCCDL,
            6800: max(8, cls._min_cycles(4_705, tCK_ps)),
            7200: max(8, cls._min_cycles(4_444, tCK_ps)),
            7600: max(8, cls._min_cycles(4_210, tCK_ps)),
            8000: max(8, cls._min_cycles(4_000, tCK_ps)),
            8400: max(8, cls._min_cycles(4_000, tCK_ps)),
            8800: max(8, cls._min_cycles(3_863, tCK_ps)),
        }
        return nCCDM.get(rate, -1)

    @classmethod
    def _resolve_nCCDM_WR(cls, rate, tCK_ps):
        tCCDM_WR_ps = {
            3200: 20_000,
            3600: 20_000,
            4000: 20_000,
            4400: 20_000,
            4800: 20_000,
            5200: 20_000,
            5600: 20_000,
            6000: 20_000,
            6400: 20_000,
            6800: 18_823,
            7200: 17_777,
            7600: 16_842,
            8000: 16_000,
            8400: 15_238,
            8800: 14_545,
        }.get(rate)
        if tCCDM_WR_ps is None:
            return -1
        return max(32, cls._min_cycles(tCCDM_WR_ps, tCK_ps))  # rmw_write_gap

    @classmethod
    def _resolve_nWTRM(cls, rate, tCK_ps, nWTRL):
        nWTRM = {
            3200: nWTRL,
            3600: nWTRL,
            4000: nWTRL,
            4400: nWTRL,
            4800: nWTRL,
            5200: nWTRL,
            5600: nWTRL,
            6000: nWTRL,
            6400: nWTRL,
            6800: max(16, cls._min_cycles(9_411, tCK_ps)),
            7200: max(16, cls._min_cycles(8_888, tCK_ps)),
            7600: max(16, cls._min_cycles(8_421, tCK_ps)),
            8000: max(16, cls._min_cycles(8_000, tCK_ps)),
            8400: max(16, cls._min_cycles(7_619, tCK_ps)),
            8800: max(16, cls._min_cycles(7_272, tCK_ps)),
        }
        return nWTRM.get(rate, -1)

    @staticmethod
    def _resolve_nRTW(rate, nCL, nBL, nCWL):
        # Shortest legal WPRE mode, with zero Read DQS offset.
        preamble = {
            3200: (2, 0),
            3600: (2, 0),
            4000: (2, 0),
            4400: (2, 0),
            4800: (2, 0),
            5200: (3, 1),
            5600: (3, 1),
            6000: (3, 1),
            6400: (3, 1),
            6800: (4, 1),
            7200: (4, 1),
            7600: (4, 1),
            8000: (4, 1),
            8400: (4, 1),
            8800: (4, 1),
        }.get(rate)
        if preamble is None:
            return -1
        wpre, rpst_extension = preamble
        return nCL + nBL + 2 + rpst_extension + wpre - nCWL


# ---- DDR5 JEDEC preset data ----

DDR5.org_presets = {
    "DDR5_8Gb_x4":   {"density": 8192,  "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 2, "row": 1<<16, "column": 1<<11, "prac": "off"},
    "DDR5_8Gb_x8":   {"density": 8192,  "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 2, "row": 1<<16, "column": 1<<10, "prac": "off"},
    "DDR5_8Gb_x16":  {"density": 8192,  "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 2, "row": 1<<16, "column": 1<<10, "prac": "off"},
    "DDR5_16Gb_x4":  {"density": 16384, "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<16, "column": 1<<11, "prac": "off"},
    "DDR5_16Gb_x8":  {"density": 16384, "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<16, "column": 1<<10, "prac": "off"},
    "DDR5_16Gb_x16": {"density": 16384, "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<16, "column": 1<<10, "prac": "off"},
    "DDR5_32Gb_x4":  {"density": 32768, "dq": 4,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<17, "column": 1<<11, "prac": "off"},
    "DDR5_32Gb_x8":  {"density": 32768, "dq": 8,  "channel_width": 32, "rank": 1, "bankgroup": 8, "bank": 4, "row": 1<<17, "column": 1<<10, "prac": "off"},
    "DDR5_32Gb_x16": {"density": 32768, "dq": 16, "channel_width": 32, "rank": 1, "bankgroup": 4, "bank": 4, "row": 1<<17, "column": 1<<10, "prac": "off"},
}

# Primary timing presets; organization- and rate-dependent timings are
# finalized by resolve_secondary_timings().
DDR5.timing_presets = {
    # DDR5-3200 (tCK = 625 ps)
    "DDR5_3200AN": {
        "rate": 3200, "nBL": 8, "nCL": 24, "nRCD": 24, "nRP": 24,
        "nRAS": 52, "nRC": 76, "nWR": 48, "nRTP": 12, "nCWL": 22, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 8, "nCCDS_WR": 8, "nWTRS": 4, "nWTRL": 16, "tCK_ps": 625,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_3200B": {
        "rate": 3200, "nBL": 8, "nCL": 26, "nRCD": 26, "nRP": 26,
        "nRAS": 52, "nRC": 78, "nWR": 48, "nRTP": 12, "nCWL": 24, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 8, "nCCDS_WR": 8, "nWTRS": 4, "nWTRL": 16, "tCK_ps": 625,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_3200C": {
        "rate": 3200, "nBL": 8, "nCL": 28, "nRCD": 28, "nRP": 28,
        "nRAS": 52, "nRC": 80, "nWR": 48, "nRTP": 12, "nCWL": 26, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 8, "nCCDS_WR": 8, "nWTRS": 4, "nWTRL": 16, "tCK_ps": 625,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-3600 (tCK = 555 ps)
    "DDR5_3600AN": {
        "rate": 3600, "nBL": 8, "nCL": 26, "nRCD": 26, "nRP": 26,
        "nRAS": 58, "nRC": 84, "nWR": 54, "nRTP": 14, "nCWL": 24, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 9, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 18, "tCK_ps": 555,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_3600B": {
        "rate": 3600, "nBL": 8, "nCL": 30, "nRCD": 30, "nRP": 30,
        "nRAS": 58, "nRC": 88, "nWR": 54, "nRTP": 14, "nCWL": 28, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 9, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 18, "tCK_ps": 555,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_3600C": {
        "rate": 3600, "nBL": 8, "nCL": 32, "nRCD": 32, "nRP": 32,
        "nRAS": 58, "nRC": 90, "nWR": 54, "nRTP": 14, "nCWL": 30, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 9, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 18, "tCK_ps": 555,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-4000 (tCK = 500 ps)
    "DDR5_4000AN": {
        "rate": 4000, "nBL": 8, "nCL": 28, "nRCD": 28, "nRP": 28,
        "nRAS": 64, "nRC": 92, "nWR": 60, "nRTP": 15, "nCWL": 26, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 10, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 20, "tCK_ps": 500,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4000B": {
        "rate": 4000, "nBL": 8, "nCL": 32, "nRCD": 32, "nRP": 32,
        "nRAS": 64, "nRC": 96, "nWR": 60, "nRTP": 15, "nCWL": 30, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 10, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 20, "tCK_ps": 500,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4000C": {
        "rate": 4000, "nBL": 8, "nCL": 36, "nRCD": 35, "nRP": 35,
        "nRAS": 64, "nRC": 99, "nWR": 60, "nRTP": 15, "nCWL": 34, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 10, "nCCDS_WR": 8, "nWTRS": 5, "nWTRL": 20, "tCK_ps": 500,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-4400 (tCK = 454 ps)
    "DDR5_4400AN": {
        "rate": 4400, "nBL": 8, "nCL": 32, "nRCD": 32, "nRP": 32,
        "nRAS": 71, "nRC": 103, "nWR": 66, "nRTP": 17, "nCWL": 30, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 11, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 22, "tCK_ps": 454,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4400B": {
        "rate": 4400, "nBL": 8, "nCL": 36, "nRCD": 36, "nRP": 36,
        "nRAS": 71, "nRC": 107, "nWR": 66, "nRTP": 17, "nCWL": 34, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 11, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 22, "tCK_ps": 454,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4400C": {
        "rate": 4400, "nBL": 8, "nCL": 40, "nRCD": 39, "nRP": 39,
        "nRAS": 71, "nRC": 110, "nWR": 66, "nRTP": 17, "nCWL": 38, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 11, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 22, "tCK_ps": 454,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-4800 (tCK = 416 ps)
    "DDR5_4800AN": {
        "rate": 4800, "nBL": 8, "nCL": 34, "nRCD": 34, "nRP": 34,
        "nRAS": 77, "nRC": 111, "nWR": 72, "nRTP": 18, "nCWL": 32, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 12, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 24, "tCK_ps": 416,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4800B": {
        "rate": 4800, "nBL": 8, "nCL": 40, "nRCD": 39, "nRP": 39,
        "nRAS": 77, "nRC": 116, "nWR": 72, "nRTP": 18, "nCWL": 38, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 12, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 24, "tCK_ps": 416,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_4800C": {
        "rate": 4800, "nBL": 8, "nCL": 42, "nRCD": 42, "nRP": 42,
        "nRAS": 77, "nRC": 119, "nWR": 72, "nRTP": 18, "nCWL": 40, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 12, "nCCDS_WR": 8, "nWTRS": 6, "nWTRL": 24, "tCK_ps": 416,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-5200 (tCK = 384 ps)
    "DDR5_5200AN": {
        "rate": 5200, "nBL": 8, "nCL": 38, "nRCD": 38, "nRP": 38,
        "nRAS": 84, "nRC": 122, "nWR": 78, "nRTP": 20, "nCWL": 36, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 13, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 26, "tCK_ps": 384,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_5200B": {
        "rate": 5200, "nBL": 8, "nCL": 42, "nRCD": 42, "nRP": 42,
        "nRAS": 84, "nRC": 126, "nWR": 78, "nRTP": 20, "nCWL": 40, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 13, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 26, "tCK_ps": 384,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_5200C": {
        "rate": 5200, "nBL": 8, "nCL": 46, "nRCD": 46, "nRP": 46,
        "nRAS": 84, "nRC": 130, "nWR": 78, "nRTP": 20, "nCWL": 44, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 13, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 26, "tCK_ps": 384,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-5600 (tCK = 357 ps)
    "DDR5_5600AN": {
        "rate": 5600, "nBL": 8, "nCL": 40, "nRCD": 40, "nRP": 40,
        "nRAS": 90, "nRC": 130, "nWR": 84, "nRTP": 21, "nCWL": 38, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 14, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 28, "tCK_ps": 357,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_5600B": {
        "rate": 5600, "nBL": 8, "nCL": 46, "nRCD": 45, "nRP": 45,
        "nRAS": 90, "nRC": 135, "nWR": 84, "nRTP": 21, "nCWL": 44, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 14, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 28, "tCK_ps": 357,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_5600C": {
        "rate": 5600, "nBL": 8, "nCL": 50, "nRCD": 49, "nRP": 49,
        "nRAS": 90, "nRC": 139, "nWR": 84, "nRTP": 21, "nCWL": 48, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 14, "nCCDS_WR": 8, "nWTRS": 7, "nWTRL": 28, "tCK_ps": 357,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-6000 (tCK = 333 ps)
    "DDR5_6000AN": {
        "rate": 6000, "nBL": 8, "nCL": 42, "nRCD": 42, "nRP": 42,
        "nRAS": 96, "nRC": 138, "nWR": 90, "nRTP": 23, "nCWL": 40, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 15, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 30, "tCK_ps": 333,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6000B": {
        "rate": 6000, "nBL": 8, "nCL": 48, "nRCD": 48, "nRP": 48,
        "nRAS": 96, "nRC": 144, "nWR": 90, "nRTP": 23, "nCWL": 46, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 15, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 30, "tCK_ps": 333,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6000C": {
        "rate": 6000, "nBL": 8, "nCL": 54, "nRCD": 53, "nRP": 53,
        "nRAS": 96, "nRC": 149, "nWR": 90, "nRTP": 23, "nCWL": 52, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 15, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 30, "tCK_ps": 333,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-6400 (tCK = 312 ps)
    "DDR5_6400AN": {
        "rate": 6400, "nBL": 8, "nCL": 46, "nRCD": 46, "nRP": 46,
        "nRAS": 103, "nRC": 149, "nWR": 96, "nRTP": 24, "nCWL": 44, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 16, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 32, "tCK_ps": 312,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6400B": {
        "rate": 6400, "nBL": 8, "nCL": 52, "nRCD": 52, "nRP": 52,
        "nRAS": 103, "nRC": 155, "nWR": 96, "nRTP": 24, "nCWL": 50, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 16, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 32, "tCK_ps": 312,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6400C": {
        "rate": 6400, "nBL": 8, "nCL": 56, "nRCD": 56, "nRP": 56,
        "nRAS": 103, "nRC": 159, "nWR": 96, "nRTP": 24, "nCWL": 54, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 16, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 32, "tCK_ps": 312,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-6800 (tCK = 294 ps)
    "DDR5_6800AN": {
        "rate": 6800, "nBL": 8, "nCL": 48, "nRCD": 48, "nRP": 48,
        "nRAS": 109, "nRC": 157, "nWR": 102, "nRTP": 26, "nCWL": 46, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 17, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 34, "tCK_ps": 294,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6800B": {
        "rate": 6800, "nBL": 8, "nCL": 56, "nRCD": 55, "nRP": 55,
        "nRAS": 109, "nRC": 164, "nWR": 102, "nRTP": 26, "nCWL": 54, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 17, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 34, "tCK_ps": 294,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_6800C": {
        "rate": 6800, "nBL": 8, "nCL": 60, "nRCD": 60, "nRP": 60,
        "nRAS": 109, "nRC": 169, "nWR": 102, "nRTP": 26, "nCWL": 58, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 17, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 34, "tCK_ps": 294,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-7200 (tCK = 277 ps)
    "DDR5_7200AN": {
        "rate": 7200, "nBL": 8, "nCL": 52, "nRCD": 52, "nRP": 52,
        "nRAS": 116, "nRC": 168, "nWR": 108, "nRTP": 27, "nCWL": 50, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 18, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 36, "tCK_ps": 277,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_7200B": {
        "rate": 7200, "nBL": 8, "nCL": 58, "nRCD": 58, "nRP": 58,
        "nRAS": 116, "nRC": 174, "nWR": 108, "nRTP": 27, "nCWL": 56, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 18, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 36, "tCK_ps": 277,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_7200C": {
        "rate": 7200, "nBL": 8, "nCL": 64, "nRCD": 63, "nRP": 63,
        "nRAS": 116, "nRC": 179, "nWR": 108, "nRTP": 27, "nCWL": 62, "nPPD": 2,
        "nCCDS": 8, "nCCDL": 18, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 36, "tCK_ps": 277,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-7600 (tCK = 263 ps)
    "DDR5_7600AN": {
        "rate": 7600, "nBL": 8, "nCL": 54, "nRCD": 54, "nRP": 54,
        "nRAS": 122, "nRC": 176, "nWR": 114, "nRTP": 29, "nCWL": 52, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 19, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 38, "tCK_ps": 263,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_7600B": {
        "rate": 7600, "nBL": 8, "nCL": 62, "nRCD": 61, "nRP": 61,
        "nRAS": 122, "nRC": 183, "nWR": 114, "nRTP": 29, "nCWL": 60, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 19, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 38, "tCK_ps": 263,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_7600C": {
        "rate": 7600, "nBL": 8, "nCL": 68, "nRCD": 67, "nRP": 67,
        "nRAS": 122, "nRC": 189, "nWR": 114, "nRTP": 29, "nCWL": 66, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 19, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 38, "tCK_ps": 263,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-8000 (tCK = 250 ps)
    "DDR5_8000AN": {
        "rate": 8000, "nBL": 8, "nCL": 56, "nRCD": 56, "nRP": 56,
        "nRAS": 128, "nRC": 184, "nWR": 120, "nRTP": 30, "nCWL": 54, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 20, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 40, "tCK_ps": 250,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8000B": {
        "rate": 8000, "nBL": 8, "nCL": 64, "nRCD": 64, "nRP": 64,
        "nRAS": 128, "nRC": 192, "nWR": 120, "nRTP": 30, "nCWL": 62, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 20, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 40, "tCK_ps": 250,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8000C": {
        "rate": 8000, "nBL": 8, "nCL": 70, "nRCD": 70, "nRP": 70,
        "nRAS": 128, "nRC": 198, "nWR": 120, "nRTP": 30, "nCWL": 68, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 20, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 40, "tCK_ps": 250,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-8400 (tCK = 238 ps), JESD79-5C Table 294
    "DDR5_8400AN": {
        "rate": 8400, "nBL": 8, "nCL": 60, "nRCD": 60, "nRP": 60,
        "nRAS": 135, "nRC": 195, "nWR": 126, "nRTP": 32, "nCWL": 58, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 21, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 42, "tCK_ps": 238,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8400B": {
        "rate": 8400, "nBL": 8, "nCL": 68, "nRCD": 68, "nRP": 68,
        "nRAS": 135, "nRC": 203, "nWR": 126, "nRTP": 32, "nCWL": 66, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 21, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 42, "tCK_ps": 238,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8400C": {
        "rate": 8400, "nBL": 8, "nCL": 74, "nRCD": 74, "nRP": 74,
        "nRAS": 135, "nRC": 209, "nWR": 126, "nRTP": 32, "nCWL": 72, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 21, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 42, "tCK_ps": 238,
        "nCS": 2,  # Ramulator guesstimate
    },
    # DDR5-8800 (tCK = 227 ps)
    "DDR5_8800AN": {
        "rate": 8800, "nBL": 8, "nCL": 62, "nRCD": 62, "nRP": 62,
        "nRAS": 141, "nRC": 203, "nWR": 132, "nRTP": 33, "nCWL": 60, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 22, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 44, "tCK_ps": 227,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8800B": {
        "rate": 8800, "nBL": 8, "nCL": 72, "nRCD": 71, "nRP": 71,
        "nRAS": 141, "nRC": 212, "nWR": 132, "nRTP": 33, "nCWL": 70, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 22, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 44, "tCK_ps": 227,
        "nCS": 2,  # Ramulator guesstimate
    },
    "DDR5_8800C": {
        "rate": 8800, "nBL": 8, "nCL": 78, "nRCD": 77, "nRP": 77,
        "nRAS": 141, "nRC": 218, "nWR": 132, "nRTP": 33, "nCWL": 76, "nPPD": 4,
        "nCCDS": 8, "nCCDL": 22, "nCCDS_WR": 8, "nWTRS": 8, "nWTRL": 44, "tCK_ps": 227,
        "nCS": 2,  # Ramulator guesstimate
    },
}
