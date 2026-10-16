"""
 GDDR6 DRAM module

"""

import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint

# Values of the "bank_group_mode" organization field; see
# GDDR6._resolve_bank_group_mode.
BANK_GROUP_MODES = ("on", "off")


class GDDR6(DRAMStandard):
    """ GDDR6 DRAM module """

    name = "GDDR6"
    internal_prefetch_size = 16
    read_latency = "nCL + nBL"


    # Hierarchy
    levels = {
        "Channel" : "N_A",
        "BankGroup" : "N_A",
        "Bank" : "Closed",
        "Row" : "Closed",
        "Column" : "N_A"
    }


    # Commands
    commands = [
        "ACT", "PREab", "PREpb",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
    ]

    #Taken from Table 30 - Trueth Table Commands
    command_cycles = {"ACT": 1, "RD": 1, "RDA": 1, "WR": 1, "WRA": 1}


    # States
    states = ["Opened", "Closed", "N_A"]


    # Timing Parameters
    timing_params = [
        "rate", "nBL",
        "nCL", "nRCDRD", "nRCDWR", "nRP", "nRAS", "nRC",
        "nWR", "nRTP", "nCWL",
        "nCCDS", "nCCDL",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW",
        "nRFCpb", "nRREFD", "nREFI", "nREFIpb",  # Refresh
        "tCK_ps",
        "nRFCab",
        "nPPD",
    ]


    supported_requests = {"Read": "RD", "Write": "WR"}


    timing_constraints = [
        # Channel
        # JESD250C Section 4.3, Figure 9, and Section 6.4 define the burst and
        # column-command spacing. nCCDS equals nBL for the modeled presets.
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),

        # Channel
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        TimingConstraint(
            level="Channel",
            preceding=["WR", "WRA"],
            following=["RD", "RDA"],
            latency="nCWL + nBL + nWTRS",
        ),
        TimingConstraint(level="Channel", preceding=["RD", "RDA"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Channel", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["ACT", "REFpb"], latency="nFAW", window=4),
        TimingConstraint(level="Channel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["PREab", "PREpb"], following=["PREab", "PREpb"], latency="nPPD"),

        # RAS <-> REFab
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        # JESD250C Table 47 Note 3 (PDF p.126): "A bank must be in the idle
        # state with tRP satisfied before it is refreshed."
        TimingConstraint(level="Channel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Channel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Channel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Channel", preceding=["REFab"], following=[
            "ACT", "PREab", "PREpb", "RD", "WR", "RDA", "WRA", "REFab", "REFpb",
        ], latency="nRFCab"),

        # RAS <-> REFpb
        TimingConstraint(level="Channel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        TimingConstraint(level="Channel", preceding=["PREab"], following=["REFpb"], latency="nRP"),
        # RDA/WRA -> REFpb recovery applies to the same bank only (JESD250C
        # Table 53, PDF p.138); see the Bank-level rules below.
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
        TimingConstraint(level="Channel", preceding=["REFpb"], following=["REFab"], latency="nRFCpb"),

        # Same Bank Group
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(
            level="BankGroup",
            preceding=["WR", "WRA"],
            following=["RD", "RDA"],
            latency="nCWL + nBL + nWTRL",
        ),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT", "REFpb"], latency="nRRDL"),

        # Bank
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA"], latency="nRCDRD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["WR", "WRA"], latency="nRCDWR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),

        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        # JESD250C Table 53 (PDF p.138), WRITE -> READ w/ AP (same bank):
        # WLmrs + BL/8 + MAX[RU(tWR/tCK) - tRTP, RU(tWTR/tCK)].
        TimingConstraint(
            level="Bank",
            preceding=["WR"],
            following=["RDA"],
            latency="nCWL + nBL + max(nWR - nRTP, nWTRL)",
        ),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT"], latency="nCWL + nBL + nWR + nRP"),

        # Bank RAS <-> REFpb
        # JESD250C Table 53 (PDF p.138): RDA/WRA -> ACTIVATE or REFpb (same bank).
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["REFpb"], latency="nRP + nRTP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["REFpb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT", "REFpb"], latency="nRFCpb"),

    ]

    @classmethod
    def validate_organization(cls, org_dict):
        super().validate_organization(org_dict)
        mode = org_dict["bank_group_mode"]
        if mode not in BANK_GROUP_MODES:
            raise ValueError(
                f"{cls.name}: bank_group_mode must be one of {BANK_GROUP_MODES}, got {mode!r}"
            )

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        device_density = org_dict["device_density"]
        cls._resolve_bank_group_mode(timing_dict, org_dict["bank_group_mode"])
        timing_dict["nRTW"] = cls._resolve_nRTW(
            timing_dict["nCL"], timing_dict["nBL"], timing_dict["nCWL"]
        )
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(
            device_density, tCK_ps
        )
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            org_dict["bankgroup"], org_dict["bank"], tCK_ps
        )
        timing_dict["nRFCab"] = cls._resolve_nRFCab(
            device_density, tCK_ps
        )

    # Bank groups (JESD250C MR3 OP11, PDF p.56; Table 21, PDF p.23). "on": same-group
    # accesses use tCCDL, tRRDL, tWTRL and tRTPL. "off": tCCDL = tCCDS, tRRDL = tRRDS,
    # tWTRL = tWTRS and tRTP = tRTPS. The switch is applied at config time.
    @staticmethod
    def _resolve_bank_group_mode(timing_dict, mode):
        if mode == "on":
            timing_dict["nRTP"] = timing_dict.get("nRTPL", -1)
        elif mode == "off":
            timing_dict["nCCDL"] = timing_dict["nCCDS"]
            timing_dict["nRRDL"] = timing_dict["nRRDS"]
            timing_dict["nWTRL"] = timing_dict["nWTRS"]
            timing_dict["nRTP"] = timing_dict.get("nRTPS", -1)

    @staticmethod
    def _resolve_nRTW(nCL, nBL, nCWL):
        # Samsung K4Z80325BC Rev. 1.3 Tables 92 and 93 define tRTW;
        # JESD250C Table 73 Note 38 leaves only bus turnaround to the system.
        # === Ramulator Guesstimate ===
        bus_turnaround = 0
        # =============================
        return nCL + 2 * nBL - nCWL + 3 + bus_turnaround

    @staticmethod
    def _resolve_nRFCpb(device_density, tCK_ps):
        # Samsung K4Z80325BC Rev. 1.3 Tables 92 and 93 (8 Gb device).
        tRFCpb_ps = {8192: 60_000}.get(device_density)
        if tRFCpb_ps is None:
            return -1
        return math.ceil(tRFCpb_ps / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return math.floor(1_900_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(bankgroup, bank, tCK_ps):
        # JESD250C Section 7.17 and Table 73: REFpb uses tREFI / 16.
        bank_count = bankgroup * bank
        if bank_count != 16:
            return -1
        return math.floor(1_900_000 / (bank_count * tCK_ps))

    @staticmethod
    def _resolve_nRFCab(device_density, tCK_ps):
        # Samsung K4Z80325BC Rev. 1.3 Tables 92 and 93 (8 Gb device).
        tRFCab_ps = {8192: 120_000}.get(device_density)
        if tRFCab_ps is None:
            return -1
        return math.ceil(tRFCab_ps / tCK_ps)

# JESD250C Table 19 defines two channels per GDDR6 device. One Ramulator
# GDDR6 instance models one channel; channels_per_device describes the device.
GDDR6.org_presets = {
    "GDDR6_8Gb_x8": {
        "device_density": 8192,
        "channel_density": 4096,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<11,
    },
    "GDDR6_8Gb_x16": {
        "device_density": 8192,
        "channel_density": 4096,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<10,
    },
    "GDDR6_16Gb_x8": {
        "device_density": 16384,
        "channel_density": 8192,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<15,
        "column": 1<<11,
    },
    "GDDR6_16Gb_x16": {
        "device_density": 16384,
        "channel_density": 8192,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<14,
        "column": 1<<11,
    },
    "GDDR6_32Gb_x8": {
        "device_density": 32768,
        "channel_density": 16384,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 8,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<16,
        "column": 1<<11,
    },
    "GDDR6_32Gb_x16": {
        "device_density": 32768,
        "channel_density": 16384,
        "channels_per_device": 2,
        "bank_group_mode": "on",
        "dq": 16,
        "channel_width": 16,
        "bankgroup": 4,
        "bank": 4,
        "row": 1<<15,
        "column": 1<<11,
    },
}
GDDR6.timing_presets = {
    # Samsung K4Z80325BC Rev. 1.3, Table 92 (8 Gb, 1.35 V, DDR WCK), tCK = 572 ps;
    # nCL from Table 91 (PDF p.155).
    "GDDR6_14000_1350mV_double": {
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 27, "nRCDWR": 16,
        "nRP": 27, "nRAS": 53, "nRC": 79,
        "nWR": 27,
        # === Ramulator Guesstimate ===
        # MR0 WLmrs 5-8 (Table 27, PDF p.47)
        "nCWL": 6,
        # =============================
        # Table 92 (PDF p.157): tCCDS = 2 tCK, tRTPL = 4 tCK, tRTPS = 2 tCK.
        "nCCDS": 2, "nRTPL": 4, "nRTPS": 2,
        # tCCDL = 3 or 4 tCK (JESD250C MR3 OP10); Table 92/93: 3/4.
        "nCCDL": 4,
        # Table 92 (PDF p.157): tRRDS = tRRDL = max(4 ns, 2 tCK), tFAW = max(16 ns, 8 tCK), tRREFD = max(8 ns, 2 tCK)
        "nRRDS": 7, "nRRDL": 7, "nFAW": 28, "nRREFD": 14,
        # Table 92 (PDF p.158): tWTRS = 2 tCK + 4 ns, tWTRL = 4 tCK + 4 ns
        "nWTRS": 9, "nWTRL": 11,
        "tCK_ps": 572,
        "nPPD": 1,
    },
    # Samsung K4Z80325BC Rev. 1.3, Table 93 (8 Gb, 1.25 V, DDR WCK), tCK = 572 ps;
    # nCL from Table 91 (PDF p.155).
    "GDDR6_14000_1250mV_double": {  # Used in Tests
        "rate": 14000, "nBL": 2, "nCL": 24,
        "nRCDRD": 30, "nRCDWR": 20,
        "nRP": 30, "nRAS": 60, "nRC": 90,
        "nWR": 30,
        # === Ramulator Guesstimate ===
        # MR0 WLmrs 5-8 (Table 27, PDF p.47)
        "nCWL": 6,
        # =============================
        # Table 93 (PDF p.161): tCCDS = 2 tCK, tRTPL = 4 tCK, tRTPS = 2 tCK.
        "nCCDS": 2, "nRTPL": 4, "nRTPS": 2,
        # tCCDL = 3 or 4 tCK (JESD250C MR3 OP10); Table 92/93: 3/4.
        "nCCDL": 4,
        # Table 93 (PDF p.160): tRRDS = tRRDL = max(6 ns, 2 tCK), tFAW = max(24 ns, 8 tCK), tRREFD = max(12 ns, 2 tCK)
        "nRRDS": 11, "nRRDL": 11, "nFAW": 42, "nRREFD": 21,
        # Table 93 (PDF p.161): tWTRS = 2 tCK + 4 ns, tWTRL = 4 tCK + 4 ns
        "nWTRS": 9, "nWTRL": 11,
        "tCK_ps": 572,
        "nPPD": 2,
    },
}
