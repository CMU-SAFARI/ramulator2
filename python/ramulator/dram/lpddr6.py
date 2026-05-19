import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class LPDDR6(DRAMStandard):
    name = "LPDDR6"
    # Temporary payload-only encoding: Ramulator derives tx_bytes from
    # internal_prefetch_size * channel_width / 8, but LPDDR6 BL24 is physically
    # 12 DQ x 24 beats with 32 B payload plus non-payload fields. Use this
    # fake/logical 16n value with the fake/logical x16 width below to make the
    # existing tx_bytes path produce 32 B until payload size is modeled directly.
    internal_prefetch_size = 16
    read_latency = "nRL + nBL_min"

    # Payload-only BL24 model. The logical x16 width gives 32 B transactions;
    # physical LPDDR6 BL24 is 12 DQ x 24 beats plus non-payload fields.
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
        "CAS_RD", "CAS_WR",
        "RD_S", "WR_S", "RDA_S", "WRA_S",
        "REFab",
    ]

    # LPDDR6 commands use an every-other-CK command protocol.
    command_cycles = {
        "ACT1": 2, "ACT2": 2,
        "PREpb": 2, "PREab": 2,
        "CAS_RD": 2, "CAS_WR": 2,
        "RD_S": 2, "WR_S": 2, "RDA_S": 2, "WRA_S": 2,
        "REFab": 2,
    }

    states = ["Opened", "Closed", "Activating", "N_A"]

    timing_params = [
        "rate", "nBL_min", "nBL_max", "nRL", "nWL",
        "nRCDr", "nRCDw", "nRP", "nRPab", "nRAS", "nRC",
        "nWTP", "nRTP", "nPPD",
        "nCCDS", "nCCDL", "nCCDS_WR", "nCCDL_WR",
        "nRRD", "nWTRS", "nWTRL", "nRTW_S", "nRTW_L",
        "nFAW", "nRFC", "nREFI",
        "nWCKPST", "nCAS", "nAAD", "nCS", "tCK_ps",
    ]

    supported_requests = {
        "Read": "RD_S",
        "Write": "WR_S",
    }

    timing_constraints = [
        # Bus occupancy constraints are auto-generated from command_cycles.
        TimingConstraint(level="Channel", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S"], latency="nBL_min"),
        TimingConstraint(level="Channel", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S"], latency="nBL_min"),

        # CAS WCK2CK sync to access.
        TimingConstraint(level="Bank", preceding=["CAS_RD"], following=["RD_S", "RDA_S"], latency="nCAS"),
        TimingConstraint(level="Bank", preceding=["CAS_WR"], following=["WR_S", "WRA_S"], latency="nCAS"),

        # Rank-level different-BG column timing and turnarounds.
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S"], latency="nCCDS"),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S"], latency="nCCDS_WR"),
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["WR_S", "WRA_S"], latency="nRTW_S"),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S"], latency="nWL + nBL_min + nWTRS"),

        # Rank switching shape retained conservatively for v1.
        TimingConstraint(level="Rank", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S", "WR_S", "WRA_S"], latency="nBL_min + nCS", window=1, sibling=True),
        TimingConstraint(level="Rank", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S"], latency="nRL + nBL_min + nCS - nWL", window=1, sibling=True),

        # Direct all-bank precharge after column commands.
        TimingConstraint(level="Rank", preceding=["RD_S"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="Rank", preceding=["WR_S"], following=["PREab"], latency="nWL + nBL_max + nWTP"),

        # RAS and activation constraints.
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nRRD"),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["ACT1"], latency="nFAW", window=4),
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="Rank", preceding=["PREab"], following=["ACT1"], latency="nRPab"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),

        # All-bank refresh only for v1.
        TimingConstraint(level="Rank", preceding=["ACT1"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="Rank", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="Rank", preceding=["RDA_S"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="Rank", preceding=["WRA_S"], following=["REFab"], latency="nWL + nBL_max + nWTP + nRP"),
        TimingConstraint(level="Rank", preceding=["REFab"], following=["ACT1", "PREab"], latency="nRFC"),

        # Same-bank-group constraints.
        TimingConstraint(level="BankGroup", preceding=["RD_S", "RDA_S"], following=["RD_S", "RDA_S"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR_S", "WRA_S"], following=["WR_S", "WRA_S"], latency="nCCDL_WR"),
        TimingConstraint(level="BankGroup", preceding=["RD_S", "RDA_S"], following=["WR_S", "WRA_S"], latency="nRTW_L"),
        TimingConstraint(level="BankGroup", preceding=["WR_S", "WRA_S"], following=["RD_S", "RDA_S"], latency="nWL + nBL_max + nWTRL"),
        TimingConstraint(level="BankGroup", preceding=["ACT1"], following=["ACT1"], latency="nRRD"),

        # Bank-local constraints.
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["ACT1"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["RD_S", "RDA_S"], latency="nRCDr"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["WR_S", "WRA_S"], latency="nRCDw"),
        TimingConstraint(level="Bank", preceding=["ACT1"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT1"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD_S"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR_S"], following=["PREpb"], latency="nWL + nBL_max + nWTP"),
        TimingConstraint(level="Bank", preceding=["RDA_S"], following=["ACT1"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA_S"], following=["ACT1"], latency="nWL + nBL_max + nWTP + nRP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        timing_dict["nRFC"] = cls._resolve_nRFC(org_dict["refresh_density_per_2_subchannels"], timing_dict["tCK_ps"])
        timing_dict["nREFI"] = cls._resolve_nREFI(timing_dict["tCK_ps"])

    @staticmethod
    def _resolve_nRFC(refresh_density_per_2_subchannels, tCK_ps):
        # LPDDR6 JESD209-6 Table 302 defines density per 2 sub-channels.
        if refresh_density_per_2_subchannels <= 4096:
            raise ValueError("LPDDR6: Table 302 tRFCab is TBD for 4Gb per 2 sub-channels")
        elif refresh_density_per_2_subchannels <= 8192:
            tRFC_ns = 210
        elif refresh_density_per_2_subchannels <= 16384:
            tRFC_ns = 280
        elif refresh_density_per_2_subchannels <= 32768:
            tRFC_ns = 380
        else:
            raise ValueError("LPDDR6: Table 302 tRFCab is TBD above 32Gb per 2 sub-channels")
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return math.ceil(3_906_000 / tCK_ps)


LPDDR6.org_presets = {
    "LPDDR6_16Gb_x16_payload_BL24": {
        # Logical modeled subchannel density. Do not use directly for Table 302 refresh.
        "density": 16384,
        # JEDEC Table 302 density is per 2 sub-channels: 2 * 16Gb = 32Gb.
        "refresh_density_per_2_subchannels": 32768,
        # Fake/logical x16 payload width, paired with fake/logical 16n prefetch
        # above so GenericDRAM computes tx_bytes = 32 B. This is not LPDDR6's
        # physical 12-DQ packet width.
        "dq": 16,
        "channel_width": 16,
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
        "nBL_min": 6,
        "nBL_max": 12,
        "nRL": 56,
        "nWL": 26,
        "nRCDr": 48,
        "nRCDw": 22,
        "nRP": 107,
        "nRPab": 115,
        "nRAS": 54,
        "nRC": 169,
        "nWTP": 32,
        "nRTP": 14,
        "nPPD": 4,
        "nCCDS": 6,
        "nCCDL": 10,
        "nCCDS_WR": 6,
        "nCCDL_WR": 10,
        "nRRD": 10,
        "nWTRS": 17,
        "nWTRL": 32,
        "nRTW_S": 41,
        "nRTW_L": 47,
        "nFAW": 40,
        "nWCKPST": 3,
        "nCAS": 2,
        "nAAD": 8,
        "nCS": 2,
        "tCK_ps": 375,
    },
}
