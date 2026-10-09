import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class HBM4(DRAMStandard):
    name = "HBM4"
    internal_prefetch_size = 8
    data_payload_bytes = 32  # One pseudochannel
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
        "nCCDS", "nCCDL", "nCCDR",
        "nRRDS", "nRRDL",
        "nWTRS", "nWTRL", "nRTW",
        "nFAW", "nPPD",
        "nRFC", "nRFCpb", "nRFMab", "nRFMpb",
        "nRREFD",
        "nREFI", "nREFIpb",
        "tCK_ps",
    ]

    supported_requests = {"Read": "RD", "Write": "WR"}

    timing_constraints = [
        # Pseudochannel timing
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nBL"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nBL"),
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["WR", "WRA"], latency="nRTW"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["RD", "RDA"], latency="nCWL + nBL + nWTRS"),
        TimingConstraint(level="PseudoChannel", preceding=["RD", "RDA"], following=["PREab"], latency="nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WR", "WRA"], following=["PREab"], latency="nCWL + nBL + nWR"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["ACT"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT", "REFpb", "RFMpb"], following=["ACT", "REFpb", "RFMpb"], latency="nFAW", window=4, shared_window=True),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["PREab"], latency="nRAS"),
        TimingConstraint(level="PseudoChannel", preceding=["PREab"], following=["ACT"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["REFab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["PREpb", "PREab"], latency="nPPD"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["REFab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["REFab"], latency="nCWL + nBL + nWR + nRP"),
        # JESD270-4A Table 38.
        TimingConstraint(level="PseudoChannel", preceding=["REFab"], following=["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], latency="nRFC"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["REFpb"], following=["REFab", "RFMab"], latency="nRFCpb"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["REFpb"], latency="nRRDS"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMab"], latency="nRC"),
        TimingConstraint(level="PseudoChannel", preceding=["PREpb", "PREab"], following=["RFMab"], latency="nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RDA"], following=["RFMab"], latency="nRP + nRTP"),
        TimingConstraint(level="PseudoChannel", preceding=["WRA"], following=["RFMab"], latency="nCWL + nBL + nWR + nRP"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMab"], following=["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], latency="nRFMab"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRREFD"),
        TimingConstraint(level="PseudoChannel", preceding=["RFMpb"], following=["REFab", "RFMab"], latency="nRFMpb"),
        TimingConstraint(level="PseudoChannel", preceding=["ACT"], following=["RFMpb"], latency="nRRDS"),

        # SID timing
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS"),
        TimingConstraint(level="Sid", preceding=["RD", "RDA"], following=["RD", "RDA"], latency="nCCDR", sibling=True),
        TimingConstraint(level="Sid", preceding=["WR", "WRA"], following=["WR", "WRA"], latency="nCCDS", sibling=True),

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
        TimingConstraint(level="Bank", preceding=["REFpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRFCpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["REFpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["REFpb"], latency="nRP"),
        TimingConstraint(level="Bank", preceding=["RFMpb"], following=["REFpb", "RFMpb", "ACT"], latency="nRFMpb"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["RFMpb"], latency="nRC"),
        TimingConstraint(level="Bank", preceding=["PREpb"], following=["RFMpb"], latency="nRP"),
    ]

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK_ps = timing_dict["tCK_ps"]
        channel_density = org_dict["channel_density"]
        timing_dict["nRC"] = timing_dict["nRAS"] + timing_dict["nRP"]
        timing_dict["nCCDL"] = cls._resolve_nCCDL(tCK_ps)
        timing_dict["nCCDR"] = cls._resolve_nCCDR(
            timing_dict["rate"], tCK_ps, org_dict["sid"], timing_dict["nCCDS"]
        )
        timing_dict["nRTW"] = cls._resolve_nRTW(timing_dict, tCK_ps)
        timing_dict["nRFC"] = cls._resolve_nRFC(
            org_dict["die_density"], org_dict["stack_height"],
            channel_density, tCK_ps,
        )
        timing_dict["nRFCpb"] = cls._resolve_nRFCpb(
            org_dict["die_density"], tCK_ps
        )
        timing_dict["nRFMab"] = timing_dict["nRFC"]
        timing_dict["nRFMpb"] = timing_dict["nRFCpb"]
        timing_dict["nREFI"] = cls._resolve_nREFI(tCK_ps)
        timing_dict["nREFIpb"] = cls._resolve_nREFIpb(
            tCK_ps,
            org_dict["bank"],
            org_dict["bankgroup"],
            org_dict["sid"],
        )
        timing_dict["nRREFD"] = cls._resolve_nRREFD(tCK_ps)

    @staticmethod
    def _resolve_nCCDL(tCK_ps):
        # JESD270-4A Table 108: max(4 nCK, 2.5 ns).
        # This resolves to 5 CK at HBM4-8000, preventing full throughput.
        # So we set to 4 here
        # return max(4, math.ceil(2_500 / tCK_ps))
        return 4

    @staticmethod
    def _resolve_nCCDR(rate, tCK_ps, num_sids, nCCDS):
        # JESD270-4A Table 108 Note 17 specifies tCCDR only for multi-SID
        # stacks.
        offset = {
            (8_000, 500, 1): 0,
            # === Ramulator Guesstimate ===
            (8_000, 500, 2): 2,
            (8_000, 500, 4): 2,
            (16_000, 250, 1): 0,
            # =============================
        }.get((rate, tCK_ps, num_sids))
        return nCCDS + offset if offset is not None else -1

    @staticmethod
    def _resolve_nRTW(timing_dict, tCK_ps):
        if timing_dict["rate"] == 8_000 and tCK_ps == 500:
            # JESD270-4A Tables 107 and 108, Note 18.
            base_cycles = (
                timing_dict["nCL"] + timing_dict["nBL"] - timing_dict["nCWL"]
            )
            analog_numerator = 5 * tCK_ps + 10 * (2_500 - 100)
            return base_cycles + math.ceil(analog_numerator / (10 * tCK_ps))
        if timing_dict["rate"] == 16_000 and tCK_ps == 250:
            return 50  # Ramulator guesstimate
        return -1

    @staticmethod
    def _resolve_nRFC(die_density, stack_height, channel_density, tCK_ps):
        # JESD270-4A Table 108.
        tRFC_ns = {
            (24576, 4, 3072): 360,
            (24576, 8, 6144): 410,
            (24576, 12, 9216): 450,
            (24576, 16, 12288): 490,
            (32768, 4, 4096): 400,
            (32768, 8, 8192): 450,
            (32768, 12, 12288): 490,
            (32768, 16, 16384): 530,
        }.get((die_density, stack_height, channel_density))
        if tRFC_ns is None:
            return -1
        return math.ceil(tRFC_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nRFCpb(die_density, tCK_ps):
        # JESD270-4A Table 108.
        tRFCpb_ns = {24576: 240, 32768: 280}.get(die_density)
        if tRFCpb_ns is None:
            return -1
        return math.ceil(tRFCpb_ns * 1000 / tCK_ps)

    @staticmethod
    def _resolve_nREFI(tCK_ps):
        # JESD270-4A Table 108 specifies a maximum interval of 3.9 us.
        return 3_900_000 // tCK_ps

    @staticmethod
    def _resolve_nREFIpb(tCK_ps, num_banks, num_bankgroups, num_sids):
        # JESD270-4A Table 108: tREFIpb = tREFI / banks per pseudo-channel.
        return 3_900_000 // (num_banks * num_bankgroups * num_sids * tCK_ps)

    @staticmethod
    def _resolve_nRREFD(tCK_ps):
        # HBM4 tRREFD = MAX(3*tCK, 8 ns)
        return max(3, math.ceil(8_000 / tCK_ps))



HBM4.org_presets = {
    # HBM CA already takes BL into account
    # One preset is one 64-bit JEDEC channel split into two 32-bit
    # pseudo-channels (JESD270-4A Table 4).
    "HBM4_32Gb_4Hi":  {"die_density": 32768, "channel_density": 4096,  "stack_height": 4,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 1, "bankgroup": 2, "bank": 8, "row": 1 << 14, "column": (1 << 5) << 3},
    "HBM4_32Gb_8Hi":  {"die_density": 32768, "channel_density": 8192,  "stack_height": 8,  "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 2, "bankgroup": 2, "bank": 8, "row": 1 << 14, "column": (1 << 5) << 3},
    "HBM4_32Gb_16Hi": {"die_density": 32768, "channel_density": 16384, "stack_height": 16, "dq": 32, "channel_width": 64, "pseudochannel": 2, "sid": 4, "bankgroup": 2, "bank": 8, "row": 1 << 14, "column": (1 << 5) << 3},
}

HBM4.timing_presets = {
    "HBM4_8000Mbps": {
        "rate": 8000, "nBL": 2,
        "nCCDS": 2,
        # === Ramulator Guesstimate ===
        "nCL": 20, "nCWL": 10,
        "nRAS": 57, "nRP": 33,
        "nRCDRD": 39, "nRCDWR": 19,
        "nRRDL": 7, "nRRDS": 5, "nFAW": 30,
        "nRTP": 12, "nWR": 42,
        "nWTRL": 13, "nWTRS": 9,
        # =============================
        "nPPD": 2, "tCK_ps": 500,
    },
    "HBM4_16000Mbps": {
        "nCWL": -1,
        # === Ramulator Guesstimate ===
        "rate": 16000, "nBL": 2, "nCL": 40,
        "nRAS": 114, "nRP": 66,
        "nRCDRD": 78, "nRCDWR": 38,
        "nRRDL": 14, "nRRDS": 10, "nFAW": 60,
        "nRTP": 24, "nWR": 84, "nCCDS": 2,
        "nWTRL": 26, "nWTRS": 18,
        "nPPD": 2, "tCK_ps": 250,
        # =============================
    },
}

# Iso-HBM4 baseline from FB-Banks: Adhinarayanan et al., "Folded Banks: 3D-Stacked HBM Design
# for Fine-Grained Random-Access Bandwidth", ISCA 2025, Table 1, column Iso-HBM4 (the baseline,
# not the FB-HBM design). Core timings in ns, projected by the paper from vendor HBM3 data,
# converted at tCK = 500 ps and rounded up. Everything the paper does not give comes from
# HBM4_8000Mbps. Its tCCDL (2 ns) is 4 CK, the resolver's value, so nCCDL is not set here.
HBM4.timing_presets["HBM4_8000Mbps_folded_banks_baseline"] = {
    **HBM4.timing_presets["HBM4_8000Mbps"],
    "nRCDRD": 32, "nRP": 32, "nRAS": 58,     # tRCD 16, tRP 16, tRAS 29 ns
    "nCL": 32, "nFAW": 32,                   # tCL 16, tFAW 16 ns
    "nRRDS": 4, "nRRDL": 4,                  # tRRD 2 ns; Table 1 has one tRRD, so both get it
}
