"""
GDDR7 DRAM.

CK4 timing unit.
"""

import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class GDDR7(DRAMStandard):
    name = "GDDR7"
    internal_prefetch_size = 32
    read_latency = "1 + nRL + nDQERL + nBL"

    levels = {
        "Channel": "N_A",
        "Bank": "Closed",
        "Row": "Closed",
        "Column": "N_A",
    }

    commands = [
        "ACT", "PREpb", "PREab",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
        "RFMab", "RFMpb",
        "RCKSTRT", "RCKSTOP",
    ]

    command_cycles = {
        "ACT": 2,
        "RD": 2,
        "RDA": 2,
        "WR": 2,
        "WRA": 2,
        "RCKSTRT": 2,
        "RCKSTOP": 2,
        "PREpb": 1,
        "PREab": 1,
        "REFpb": 1,
        "REFab": 1,
        "RFMpb": 1,
        "RFMab": 1,
    }

    row_commands = ["ACT", "PREpb", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"]
    column_commands = ["RD", "WR", "RDA", "WRA", "RCKSTRT", "RCKSTOP"]

    states = ["Opened", "Closed", "N_A"]

    timing_params = [
        "rate", "nBL", "nRL", "nWL", "nDQERL",
        "nRCDRD", "nRCDWR", "nRP", "nRAS", "nRC",
        "nRRD", "nRREFD", "nRPD", "nRTPSB", "nPPD", "nWR",
        "nCCD", "nCCDSB",
        "nWTR", "nWTRSB", "nRTW",
        "nREFI", "nREFIpb", "nRFCab", "nRFCpb", "nRDREFab", "nRFMab", "nRFMpb",
        "nRCKSTRT2RD", "nRD2RCKSTOP", "nRCKSP2ST", "nRCKST2SP",
        "nRCKSTOP_LAT", "nRCKEN", "nRCK_LS", "nRCKPST",
        "tCK_ps",
    ]

    supported_requests = {"Read": "RD", "Write": "WR"}

    timing_constraints = [
        # Column-to-column spacing, different bank.
        TimingConstraint("Channel", ["RD", "RDA"], ["RD", "RDA"], "nCCD"),
        TimingConstraint("Channel", ["WR", "WRA"], ["WR", "WRA"], "nCCD"),

        # Read/write turnaround.
        TimingConstraint("Channel", ["RD", "RDA"], ["WR", "WRA"], "nRTW"),
        TimingConstraint("Channel", ["WR", "WRA"], ["RD", "RDA"], "nWL + nBL + nWTR"),

        # Same-bank column spacing.
        TimingConstraint("Bank", ["RD", "RDA"], ["RD", "RDA"], "nCCDSB"),
        TimingConstraint("Bank", ["WR", "WRA"], ["WR", "WRA"], "nCCDSB"),
        TimingConstraint("Bank", ["WR", "WRA"], ["RD", "RDA"], "nWL + nBL + nWTRSB"),

        # Row activation/precharge.
        TimingConstraint("Channel", ["ACT"], ["ACT"], "nRRD"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["PREpb", "PREab"], "nPPD"),
        # JESD239D Table 150 (PDF p.319): tRAS, Activate to Precharge command period.
        TimingConstraint("Channel", ["ACT"], ["PREab"], "nRAS"),
        # JESD239D §6.11 (PDF p.212): a bank can be activated tRP after the Precharge command.
        TimingConstraint("Channel", ["PREab"], ["ACT"], "nRP"),
        TimingConstraint("Bank", ["ACT"], ["ACT"], "nRC"),
        TimingConstraint("Bank", ["ACT"], ["RD", "RDA"], "nRCDRD"),
        TimingConstraint("Bank", ["ACT"], ["WR", "WRA"], "nRCDWR"),
        TimingConstraint("Bank", ["ACT"], ["PREpb"], "nRAS"),
        TimingConstraint("Bank", ["PREpb"], ["ACT"], "nRP"),
        TimingConstraint("Bank", ["PREpb"], ["ACT", "REFpb", "RFMpb"], "nRPD", sibling=True),

        # Explicit precharge and auto-precharge follow-up.
        TimingConstraint("Bank", ["RD"], ["PREpb"], "nRTPSB"),
        TimingConstraint("Bank", ["WR"], ["PREpb"], "nWL + nBL + nWR"),
        # JESD239D Table 90 (PDF p.213): WRITE → READ w/AP same bank = WL + BL/8 + MAX(RU(tWR) − RU(tRTPSB), RU(tWTR))
        TimingConstraint("Bank", ["WR"], ["RDA"], "nWL + nBL + max(nWR - nRTPSB, nWTRSB)"),
        TimingConstraint("Channel", ["RD", "RDA"], ["PREab"], "nRTPSB"),
        TimingConstraint("Channel", ["WR", "WRA"], ["PREab"], "nWL + nBL + nWR"),
        TimingConstraint("Bank", ["RDA"], ["ACT", "REFpb", "RFMpb"], "nRTPSB + nRP"),
        TimingConstraint("Bank", ["WRA"], ["ACT", "REFpb", "RFMpb"], "nWL + nBL + nWR + nRP"),

        # All-bank refresh. Conservative: block normal row traffic at Channel level.
        TimingConstraint("Channel", ["ACT"], ["REFab", "RFMab"], "nRC"),
        TimingConstraint("Channel", ["RD", "RDA"], ["REFab", "RFMab"], "nRDREFab"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["REFab", "RFMab"], "nRP"),
        TimingConstraint("Channel", ["RDA"], ["REFab", "RFMab"], "nRTPSB + nRP"),
        TimingConstraint("Channel", ["WRA"], ["REFab", "RFMab"], "nWL + nBL + nWR + nRP"),
        TimingConstraint("Channel", ["REFab"], ["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], "nRFCab"),

        # Per-bank refresh. JESD239D Table 93 Note 1 (PDF p.219): REFpb to another bank waits tRREFD
        # inside a set; the tRFCpb pause between sets, which also holds RFMpb (§6.13, PDF p.220), is
        # enforced by the per-bank refresh manager, which knows where a set ends.
        TimingConstraint("Channel", ["REFpb"], ["REFpb", "RFMpb", "ACT"], "nRREFD"),
        TimingConstraint("Channel", ["REFpb"], ["REFab", "RFMab"], "nRFCpb"),
        TimingConstraint("Channel", ["ACT"], ["REFpb", "RFMpb"], "nRRD"),
        TimingConstraint("Channel", ["PREab"], ["REFpb", "RFMpb"], "nRP"),
        TimingConstraint("Bank", ["REFpb"], ["ACT", "REFpb", "RFMpb"], "nRFCpb"),
        TimingConstraint("Bank", ["ACT"], ["REFpb", "RFMpb"], "nRC"),
        TimingConstraint("Bank", ["PREpb"], ["REFpb", "RFMpb"], "nRP"),

        # JESD239D Section 6.13 applies the refresh separation rules to RFM.
        TimingConstraint("Channel", ["RFMab"], ["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], "nRFMab"),
        TimingConstraint("Channel", ["RFMpb"], ["REFpb", "RFMpb", "ACT"], "nRREFD"),
        TimingConstraint("Channel", ["RFMpb"], ["REFab", "RFMab"], "nRFMpb"),
        TimingConstraint("Bank", ["RFMpb"], ["ACT", "REFpb", "RFMpb"], "nRFMpb"),

        # RCK start/stop command timing.
        TimingConstraint("Channel", ["RCKSTRT"], ["RD", "RDA"], "nRCKSTRT2RD"),
        TimingConstraint("Channel", ["RD", "RDA"], ["RCKSTOP"], "nRD2RCKSTOP"),
        TimingConstraint("Channel", ["RCKSTOP"], ["RCKSTRT", "RD", "RDA"], "nRCKSP2ST"),
        TimingConstraint("Channel", ["RCKSTRT"], ["RCKSTOP"], "nRCKST2SP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK = timing_dict["tCK_ps"]
        device_density = org_dict["device_density"]
        rate = timing_dict["rate"]

        # JESD239D Table 150.
        timing_dict["nCCD"] = timing_dict["nBL"]
        timing_dict["nCCDSB"] = 4
        timing_dict["nPPD"] = 2
        timing_dict["nRPD"] = timing_dict["nPPD"]
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(org_dict["bank"], tCK)
        timing_dict["nRC"] = cls._resolve_nRC(
            timing_dict["nRAS"], timing_dict["nRP"]
        )
        timing_dict["nRTW"] = cls._resolve_nRTW(
            timing_dict["nRL"],
            timing_dict["nDQERL"],
            timing_dict["nBL"],
            timing_dict["nWL"],
        )
        timing_dict["nRFCab"] = cls._resolve_nRFCab(device_density, rate)
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(device_density, rate)
        timing_dict["nRDREFab"] = cls._resolve_nRDREFab(
            timing_dict["nRL"], timing_dict["nDQERL"], timing_dict["nBL"]
        )
        # === Ramulator Guesstimate ===
        timing_dict["nRFMab"] = timing_dict["nRFCab"]
        timing_dict["nRFMpb"] = timing_dict["nRFCpb"]
        # =============================

        timing_dict["nRCKSTRT2RD"] = 2
        timing_dict["nRCKPST"] = 2
        timing_dict["nRD2RCKSTOP"] = cls._resolve_nRD2RCKSTOP(
            timing_dict["nRL"],
            timing_dict["nDQERL"],
            timing_dict["nBL"],
            timing_dict["nRCKPST"],
            timing_dict["nRCKSTOP_LAT"],
        )
        timing_dict["nRCKST2SP"] = cls._resolve_nRCKST2SP(
            timing_dict["nRCKEN"], timing_dict["nRCK_LS"], timing_dict["nRCK_HS_min"]
        )

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return math.floor(1_900_000 / tCK_ps)

    @staticmethod
    def _resolve_nREFIpb(bank, tCK_ps):
        if bank != 16:
            return -1
        return max(1, math.floor(1_900_000 / (16 * tCK_ps)))

    @staticmethod
    def _resolve_nRC(nRAS, nRP):
        return nRAS + nRP

    @staticmethod
    def _resolve_nRTW(nRL, nDQERL, nBL, nWL):
        bus_turnaround = 1  # Ramulator guesstimate
        return max(1, nRL + nDQERL + nBL + 3 - nWL + bus_turnaround)

    # === Ramulator Guesstimate ===
    @staticmethod
    def _resolve_nRFCab(device_density, rate):
        # JESD239D Table 150 leaves tRFCab vendor-specific; Table 144 supplies
        # IDD test conditions, not functional AC minima.
        return {
            (16384, 28000): 315,
        }.get((device_density, rate), -1)

    @staticmethod
    def _resolve_nRFCpb(device_density, rate):
        # JESD239D Table 150 leaves tRFCpb vendor-specific; Table 144 supplies
        # IDD test conditions, not functional AC minima.
        return {
            (16384, 28000): 105,
        }.get((device_density, rate), -1)
    # =============================

    @staticmethod
    def _resolve_nRDREFab(nRL, nDQERL, nBL):
        return nRL + nDQERL + nBL + 2

    @staticmethod
    def _resolve_nRD2RCKSTOP(nRL, nDQERL, nBL, nRCKPST, nRCKSTOP_LAT):
        return max(1, nRL + nDQERL + nBL + nRCKPST - nRCKSTOP_LAT)

    @staticmethod
    def _resolve_nRCKST2SP(nRCKEN, nRCK_LS, nRCK_HS_min):
        # tRCKST2SP = RCKEN + RCK_LS + tRCK_HS(min) (JESD239D Table 150, PDF p.317; Table 88, PDF p.190).
        return nRCKEN + nRCK_LS + nRCK_HS_min


# One Ramulator GDDR7 instance models one channel; channels_per_device
# identifies the physical device's channel mode.
GDDR7.org_presets = {
    "GDDR7_16Gb_x8": {
        "device_density": 16384,
        "channel_density": 4096,
        "channels_per_device": 4,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 14,
        "column": (1 << 6) << 5,
    },
    "GDDR7_32Gb_x8": {
        "device_density": 32768,
        "channel_density": 8192,
        "channels_per_device": 4,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 15,
        "column": (1 << 6) << 5,
    },
    "GDDR7_64Gb_x8": {
        "device_density": 65536,
        "channel_density": 16384,
        "channels_per_device": 4,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 16,
        "column": (1 << 6) << 5,
    },
}


GDDR7.timing_presets = {
    "GDDR7_28000_PAM3": {
        # JESD239D Tables 1, 2, and 150; 4-channel mode at 28 Gb/s PAM3.
        # Density-dependent refresh and RFM timings are resolved separately.
        "rate": 28000, "nBL": 2,
        # === Ramulator Guesstimate ===
        "nRL": 24, "nWL": 6, "nDQERL": 0,
        "nRCDRD": 30, "nRCDWR": 19,
        "nRP": 30, "nRAS": 60,
        "nWR": 30, "nRTPSB": 4,
        "nRRD": 4, "nRREFD": 21,
        "nWTR": 9, "nWTRSB": 11,
        "nRCKEN": 6, "nRCKSTOP_LAT": 10,
        "nRCK_LS": 2, "nRCKSP2ST": 8,
        # tRCK_HS(min) is vendor specific (§6.9.2, PDF p.188); 18 = RL + tRCKSTRT2RD − RCKEN − RCK_LS, the value for a read issued tRCKSTRT2RD after RCKSTRT
        "nRCK_HS_min": 18,
        # =============================
        "tCK_ps": 571,
    },
}
