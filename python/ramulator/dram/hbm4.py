import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM4(DRAMStandard):
    name = "HBM4"
    internal_prefetch_size = 8
    tick_multiplier = 2
    read_latency = "nCL + nBL"

    levels = {
        "Channel":        "N_A",
        "PseudoChannel":  "N_A",
        "Sid":            "N_A",
        "BankGroup":      "N_A",
        "Bank":           "Closed",
        "Row":            "Closed",
        "Column":         "N_A",
    }

    commands = [
        "ACT", "PREpb", "PREab",
        "RD", "WR", "RDA", "WRA",
        "REFab", "REFpb",
        "RFMab", "RFMpb",
    ]

    command_cycles = {
        "ACT": 1.5,
        "PREpb": 0.5, "PREab": 0.5,
        "REFab": 0.5, "REFpb": 0.5,
        "RFMab": 0.5, "RFMpb": 0.5,
    }

    row_commands = ["ACT", "PREpb", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"]
    column_commands = ["RD", "WR", "RDA", "WRA"]

    states = ["Opened", "Closed", "N_A"]

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

    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

    timing_constraints = [
        # Pseudochannel timing
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        TimingConstraint(level="PseudoChannel", preceding=["RD"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WR"], following=["PREab"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nFAW", window=4),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab", "REFpb"], latency="nRFC"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFpb"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMab"], following=["ACT", "PREab", "RFMpb"], latency="nRFMab"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["RFMpb"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMpb"], latency="nRRDS"),

        # SID timing
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDR", sibling=True),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDR", sibling=True),

        # Bank-group timing
        TimingConstraint(level="BankGroup", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDL"),
        TimingConstraint(level="BankGroup", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["ACT"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["ACT"], following=["REFpb", "RFMpb"], latency="nRRDL"),
        TimingConstraint(level="BankGroup", preceding=["REFpb", "RFMpb"], following=["ACT"], latency="nRRDL"),

        # Bank timing
        TimingConstraint(level="Bank", preceding=["ACT"], following=["ACT"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RD", "RDA"], latency="nRCDRD"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["WR", "WRA"], latency="nRCDWR"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["PREpb"], latency="nRAS"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RD"], following=["PREpb"], latency="nRTP"),
        TimingConstraint(level="Bank", preceding=["WR"], following=["PREpb"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="Bank", preceding=["RDA"], following=["ACT", "REFpb", "RFMpb"], latency="nRTP + nRP"),
        TimingConstraint(level="Bank", preceding=["WRA"], following=["ACT", "REFpb", "RFMpb"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["ACT"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RFMpb"], following=["ACT"], latency="nRFMpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["RFMpb"], latency="nRP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        channel_density_mb = cls._resolve_channel_density_mb(org_dict)
        timing_dict["nRFC"] = cls._resolve_nRFC(channel_density_mb, tCK_ps)
        timing_dict["nRFMab"] = timing_dict["nRFC"]
        timing_dict["nRFMpb"] = timing_dict["nRFCpb"]
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(tCK_ps, org_dict["bank"], org_dict["bankgroup"], org_dict["sid"])
        timing_dict["nRREFD"] = cls._resolve_nRREFD(tCK_ps)

    @staticmethod
    def _resolve_channel_density_mb(org_dict):
        column = org_dict["column"] // HBM4.internal_prefetch_size
        channel_density_bits = (column
                                * org_dict["row"]
                                * org_dict["bank"]
                                * org_dict["bankgroup"]
                                * org_dict["sid"]
                                * org_dict["pseudochannel"]
                                * org_dict["dq"]
                                * 8)
        return channel_density_bits / (1024 ** 2)

    @staticmethod
    def _resolve_nRFC(channel_density_mb, tCK_ps):
        if channel_density_mb <= 4 * 1024:
            tRFC_ns = 400
        elif channel_density_mb <= 8 * 1024:
            tRFC_ns = 450
        elif channel_density_mb <= 16 * 1024:
            tRFC_ns = 530
        else:
            raise ValueError(f"HBM4 nRFC is not defined for {channel_density_mb:g} MB channel density")
        return math.ceil(tRFC_ns * 1000 / tCK_ps)
    
    @staticmethod
    def _resolve_nREFI(tCK_ps):
        return math.ceil(3_900_000 / tCK_ps)
    
    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks, num_bankgroups, num_sids):
        # HBM4 tREFIpb = tREFI / banks per pseudochannel
        trefi = 3_900_000  # ps
        return math.ceil(trefi / num_banks / num_bankgroups / num_sids / tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM4 tRREFD = MAX(3*tCK, 8 ns)
        return max(3, math.ceil(8_000 / tCK_ps))



HBM4.org_presets = {
    "HBM4_32Gb_4Hi":  {"density": 32768, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 1, "bankgroup": 2, "bank": 8, "row": 1<<14, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    "HBM4_32Gb_8Hi":  {"density": 32768, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 2, "bankgroup": 2, "bank": 8, "row": 1<<14, "column": (1<<5) << 3},  # HBM CA already takes BL into account
    "HBM4_32Gb_16Hi": {"density": 32768, "dq": 32, "channel_width": 32, "pseudochannel": 2, "sid": 4, "bankgroup": 2, "bank": 8, "row": 1<<14, "column": (1<<5) << 3},  # HBM CA already takes BL into account
}

HBM4.timing_presets = {
    "HBM4_8000Mbps": {
        "rate": 8000, "nBL": 2, "nCL": 20, "nCWL": 10,
        "nRC": 90, "nRAS": 57, "nRP": 33, "nRCDRD": 39, "nRCDWR": 19,
        "nRRDL": 7, "nRRDS": 5, "nFAW": 30, "nRTP": 12, "nWR": 42,
        "nCCDL": 4, "nCCDS": 2, "nCCDR": 2,
        "nWTRL": 13, "nWTRS": 9, "nRTW": 25,
        "nPPD": 2,
        "nRFCpb": 560, "nRREFD": 8,
        "tCK_ps": 500,
    },
    "HBM4_16000Mbps": {
        "rate": 16000, "nBL": 2, "nCL": 40, "nCWL": 20,
        "nRC": 180, "nRAS": 114, "nRP": 66, "nRCDRD": 78, "nRCDWR": 38,
        "nRRDL": 14, "nRRDS": 10, "nFAW": 60, "nRTP": 24, "nWR": 84,
        "nCCDL": 4, "nCCDS": 2, "nCCDR": 2,
        "nWTRL": 26, "nWTRS": 18, "nRTW": 50,
        "nPPD": 2,
        "nRFCpb": 1120, "nRREFD": 8,
        "tCK_ps": 250,
    },
    # HBM4 14.4 Gbps — 1.8x scaling off the 8 Gbps base. Plausible
    # third-wave HBM4 production target (SK hynix announced HBM4
    # roadmap covers ~10-16 Gbps range).
    "HBM4_14400Mbps": {
        "rate": 14400, "nBL": 2, "nCL": 36, "nCWL": 18,
        "nRC": 162, "nRAS": 103, "nRP": 60, "nRCDRD": 71, "nRCDWR": 35,
        "nRRDL": 13, "nRRDS": 9, "nFAW": 54, "nRTP": 22, "nWR": 76,
        "nCCDL": 4, "nCCDS": 2, "nCCDR": 2,
        "nWTRL": 24, "nWTRS": 17, "nRTW": 45,
        "nPPD": 2,
        "nRFCpb": 1008, "nRREFD": 8,
        "tCK_ps": 278,
    },
}
