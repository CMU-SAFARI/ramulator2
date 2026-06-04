"""
GDDR7 DRAM module.

This initial model uses one GDDR7 channel per Ramulator controller, 16 banks,
no bank groups, and CK4 as the timing unit.  The test timing preset is a smoke
configuration: source-backed GDDR7 formulas are applied first, then unresolved
core timings are filled with explicit GDDR6-derived guesstimates.
"""

import math

from ramulator.dram.spec import DRAMStandard, TimingConstraint


class GDDR7(DRAMStandard):
    """Initial GDDR7 DRAM module."""

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
        "ACT",
        "PREpb",
        "PREab",
        "RD",
        "WR",
        "RDA",
        "WRA",
        "REFab",
        "REFpb",
        "RFMab",
        "RFMpb",
        "RCKSTRT",
        "RCKSTOP",
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

    row_commands = [
        "ACT",
        "PREpb",
        "PREab",
        "REFab",
        "REFpb",
        "RFMab",
        "RFMpb",
    ]

    column_commands = [
        "RD",
        "WR",
        "RDA",
        "WRA",
        "RCKSTRT",
        "RCKSTOP",
    ]

    states = ["Opened", "Closed", "N_A"]

    timing_params = [
        "rate",
        "nBL",
        "nRL",
        "nWL",
        "nDQERL",
        "nRCDRD",
        "nRCDWR",
        "nRP",
        "nRAS",
        "nRC",
        "nRRD",
        "nRREFD",
        "nRPD",
        "nRTPSB",
        "nPPD",
        "nWR",
        "nCCD",
        "nCCDSB",
        "nWTR",
        "nWTRSB",
        "nRTW",
        "nREFI",
        "nREFIpb",
        "nRFCab",
        "nRFCpb",
        "nRDREFab",
        "nRFMab",
        "nRFMpb",
        "nRCKSTRT2RD",
        "nRD2RCKSTOP",
        "nRCKSP2ST",
        "nRCKST2SP",
        "nRCKSTOP_LAT",
        "nRCKEN",
        "nRCK_LS",
        "nRCKPST",
        "nRCK_HS",
        "tCK_ps",
    ]

    supported_requests = {
        "Read": "RD",
        "Write": "WR",
    }

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
        TimingConstraint("Bank", ["ACT"], ["ACT"], "nRC"),
        TimingConstraint("Bank", ["ACT"], ["RD", "RDA"], "nRCDRD"),
        TimingConstraint("Bank", ["ACT"], ["WR", "WRA"], "nRCDWR"),
        TimingConstraint("Bank", ["ACT"], ["PREpb"], "nRAS"),
        TimingConstraint("Bank", ["PREpb"], ["ACT"], "nRP"),
        TimingConstraint("Bank", ["PREpb"], ["ACT", "REFpb"], "nRPD", sibling=True),

        # Explicit precharge and auto-precharge follow-up.
        TimingConstraint("Bank", ["RD"], ["PREpb"], "nRTPSB"),
        TimingConstraint("Bank", ["WR"], ["PREpb"], "nWL + nBL + nWR"),
        TimingConstraint("Bank", ["RDA"], ["ACT", "REFpb", "RFMpb"], "nRTPSB + nRP"),
        TimingConstraint("Bank", ["WRA"], ["ACT", "REFpb", "RFMpb"], "nWL + nBL + nWR + nRP"),

        # All-bank refresh. Conservative: block normal row traffic at Channel level.
        TimingConstraint("Channel", ["ACT"], ["REFab"], "nRC"),
        TimingConstraint("Channel", ["RD", "RDA"], ["REFab"], "nRDREFab"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["REFab"], "nRP"),
        TimingConstraint("Channel", ["RDA"], ["REFab"], "nRTPSB + nRP"),
        TimingConstraint("Channel", ["WRA"], ["REFab"], "nWL + nBL + nWR + nRP"),
        TimingConstraint("Channel", ["REFab"], ["ACT", "PREab", "REFab", "REFpb"], "nRFCab"),

        # Per-bank refresh.
        TimingConstraint("Channel", ["REFpb"], ["REFpb", "ACT"], "nRREFD"),
        TimingConstraint("Channel", ["REFpb"], ["REFab"], "nRFCpb"),
        TimingConstraint("Channel", ["ACT"], ["REFpb"], "nRRD"),
        TimingConstraint("Bank", ["REFpb"], ["ACT", "REFpb"], "nRFCpb"),
        TimingConstraint("Bank", ["ACT"], ["REFpb"], "nRC"),
        TimingConstraint("Bank", ["PREpb"], ["REFpb"], "nRP"),

        # RFM command plumbing only. No RFM policy in this model.
        TimingConstraint("Channel", ["RFMab"], ["ACT", "PREab", "REFab", "REFpb", "RFMab", "RFMpb"], "nRFMab"),
        TimingConstraint("Bank", ["RFMpb"], ["ACT", "REFpb", "RFMpb"], "nRFMpb"),
        TimingConstraint("Channel", ["ACT"], ["RFMab"], "nRC"),
        TimingConstraint("Bank", ["ACT"], ["RFMpb"], "nRC"),
        TimingConstraint("Channel", ["PREpb", "PREab"], ["RFMab"], "nRP"),
        TimingConstraint("Bank", ["PREpb"], ["RFMpb"], "nRP"),

        # RCK start/stop command timing.
        TimingConstraint("Channel", ["RCKSTRT"], ["RD", "RDA"], "nRCKSTRT2RD"),
        TimingConstraint("Channel", ["RD", "RDA"], ["RCKSTOP"], "nRD2RCKSTOP"),
        TimingConstraint("Channel", ["RCKSTOP"], ["RCKSTRT", "RD", "RDA"], "nRCKSP2ST"),
        TimingConstraint("Channel", ["RCKSTRT"], ["RCKSTOP"], "nRCKST2SP"),
    ]

    _GDDR6_GUESSTIMATE_BASE_QUAD = {
        # Source: existing Ramulator GDDR6_2000_1250mV_quad preset.
        # These values are GDDR6-derived guesstimates only.
        # Do not present them as vendor GDDR7 timings.
        "nBL": 2,
        "nCL": 24,
        "nRCDRD": 30,
        "nRCDWD": 19,
        "nRP": 30,
        "nRAS": 60,
        "nRC": 89,
        "nWR": 30,
        "nRTP": 4,
        "nCWL": 6,
        "nCCDS": 4,
        "nCCDL": 6,
        "nRRDS": 11,
        "nRRDL": 11,
        "nWTRS": 9,
        "nWTRL": 11,
        "nRFCpb": 105,
        "nRREFD": 21,
        "nREFI": 3333,
        "tCK_ps": 570,
        "nRFCab": 315,
    }

    _GDDR6_GUESSTIMATE_BASE_DOUBLE = {
        # Source: existing Ramulator GDDR6_2000_1250mV_double preset.
        # Use only for an optional NRZ nBL=4 smoke preset.
        "nBL": 4,
        "nCL": 24,
        "nRCDRD": 30,
        "nRCDWD": 19,
        "nRP": 30,
        "nRAS": 60,
        "nRC": 89,
        "nWR": 30,
        "nRTP": 4,
        "nCWL": 6,
        "nCCDS": 4,
        "nCCDL": 6,
        "nRRDS": 11,
        "nRRDL": 17,
        "nWTRS": 9,
        "nWTRL": 11,
        "nRFCpb": 105,
        "nRREFD": 21,
        "nREFI": 3333,
        "tCK_ps": 570,
        "nRFCab": 315,
    }

    @classmethod
    def _select_gddr6_base(cls, timing_dict):
        return cls._GDDR6_GUESSTIMATE_BASE_QUAD if timing_dict.get("nBL", 2) == 2 else cls._GDDR6_GUESSTIMATE_BASE_DOUBLE

    @classmethod
    def _apply_gddr6_guesstimates(cls, timing_dict):
        base = cls._select_gddr6_base(timing_dict)

        # Latency names: GDDR7 uses RL/WL; the closest GDDR6 analogs are CL/CWL.
        timing_dict.setdefault("nRL", base["nCL"])
        timing_dict.setdefault("nWL", base["nCWL"])
        timing_dict.setdefault("nDQERL", 0)

        # Core row timings.
        timing_dict.setdefault("nRCDRD", base["nRCDRD"])
        timing_dict.setdefault("nRCDWR", base["nRCDWD"])
        timing_dict.setdefault("nRP", base["nRP"])
        timing_dict.setdefault("nRAS", base["nRAS"])
        timing_dict.setdefault("nRC", max(base["nRC"], timing_dict["nRAS"] + timing_dict["nRP"]))
        timing_dict.setdefault("nRRD", max(base["nRRDS"], base["nRRDL"]))
        timing_dict.setdefault("nRREFD", base["nRREFD"])
        timing_dict.setdefault("nRTPSB", base["nRTP"])
        timing_dict.setdefault("nWR", base["nWR"])

        # Core column/turnaround timings.
        timing_dict.setdefault("nWTR", base["nWTRS"])
        timing_dict.setdefault("nWTRSB", max(base["nWTRS"], base["nWTRL"]))

        # Refresh timings.
        timing_dict.setdefault("nRFCab", base["nRFCab"])
        timing_dict.setdefault("nRFCpb", base["nRFCpb"])

    # Burst length in CK4 per signaling mode (MR0 OP8).
    #   PAM3: 16 symbols/lane delivered in 2 CK4 (1.5 bits/symbol density).
    #   NRZ : 32 bits/lane delivered in 4 CK4 (1 bit/symbol density).
    # Payload per burst is 256 bits (32 B) in both modes — the difference is
    # wire time, which propagates through nCCD, read_latency, and rate.
    _ENCODING_NBL = {"PAM3": 2, "NRZ": 4}

    @classmethod
    def resolve_secondary_timings(cls, timing_dict, org_dict):
        tCK = timing_dict["tCK_ps"]

        # nBL is driven by the encoding (MR0 OP8). Presets must not pre-set
        # it; doing so would silently contradict the encoding choice.
        encoding = org_dict.get("encoding", "PAM3")
        if encoding not in cls._ENCODING_NBL:
            raise ValueError(
                f"GDDR7: encoding must be one of {sorted(cls._ENCODING_NBL)}, got '{encoding}'"
            )
        if "nBL" in timing_dict:
            raise ValueError(
                "GDDR7: nBL is set by 'encoding' (PAM3→2, NRZ→4); "
                "do not set nBL in the timing preset or as an override."
            )
        timing_dict["nBL"] = cls._ENCODING_NBL[encoding]

        timing_dict["rate"] = round(cls.internal_prefetch_size * 1_000_000 / (timing_dict["nBL"] * tCK))

        # Fixed/derived GDDR7 values. These are not GDDR6 guesstimates.
        timing_dict.setdefault("nCCD", timing_dict["nBL"])
        timing_dict.setdefault("nCCDSB", 4)
        timing_dict.setdefault("nPPD", 2)
        timing_dict.setdefault("nRPD", timing_dict["nPPD"])
        timing_dict.setdefault("nREFI", math.floor(1_900_000 / tCK))
        timing_dict.setdefault("nREFIpb", max(1, math.floor(timing_dict["nREFI"] / 16)))

        # Fill source gaps with direct numeric GDDR6-derived guesstimates.
        cls._apply_gddr6_guesstimates(timing_dict)

        # GDDR7 formulas that depend on resolved latency fields.
        timing_dict.setdefault(
            "nRTW",
            max(1, timing_dict["nRL"] + timing_dict["nDQERL"] + timing_dict["nBL"] + 3 - timing_dict["nWL"] + 1),
        )
        timing_dict.setdefault(
            "nRDREFab",
            timing_dict["nRL"] + timing_dict["nDQERL"] + timing_dict["nBL"] + 2,
        )

        # RCK defaults. JESD239D gives fixed values for nRCKSTRT2RD and nRCKPST;
        # MR9-valid minima are used for RCKEN, RCKSTOP_LAT, and RCK_LS.
        timing_dict.setdefault("nRCKSTRT2RD", 2)
        timing_dict.setdefault("nRCKPST", 2)
        timing_dict.setdefault("nRCKEN", 6)
        timing_dict.setdefault("nRCKSTOP_LAT", 10)
        timing_dict.setdefault("nRCK_LS", 2)

        # JESD239D Table 88 hard minimums. These are universal JEDEC bounds (not
        # vendor-specific), so they are modelable config-validity checks. The
        # defaults above are legal; these only fire on an illegal user override.
        #   tRCK_ST(min)=4 nCK4, and Note 2 requires RCKEN >= tRCK_ST(min).
        #   tRCKPST(min)=2 nCK4. tRCKSTRT2RD(min)=2 nCK4.
        #   RCK_LS (MR9 OP2) is either skipped (0) or 2 CK4 cycles.
        if timing_dict["nRCKEN"] < 4:
            raise ValueError("GDDR7: nRCKEN must be >= 4 (tRCK_ST minimum, JESD239D Table 88 Note 2)")
        if timing_dict["nRCKPST"] < 2:
            raise ValueError("GDDR7: nRCKPST must be >= 2 nCK4 (JESD239D Table 88)")
        if timing_dict["nRCKSTRT2RD"] < 2:
            raise ValueError("GDDR7: nRCKSTRT2RD must be >= 2 nCK4 (JESD239D Table 88)")
        if timing_dict["nRCK_LS"] not in (0, 2):
            raise ValueError("GDDR7: nRCK_LS must be 0 or 2 CK4 cycles (MR9 OP2, JESD239D §6.9)")


        timing_dict.setdefault(
            "nRCK_HS",
            max(0, timing_dict["nRL"] + timing_dict["nRCKSTRT2RD"] - timing_dict["nRCKEN"] - timing_dict["nRCK_LS"]),
        )
        timing_dict.setdefault(
            "nRD2RCKSTOP",
            max(1, timing_dict["nRL"] + timing_dict["nDQERL"] + timing_dict["nBL"] + timing_dict["nRCKPST"] - timing_dict["nRCKSTOP_LAT"]),
        )
        # JESD239D marks tRCKSP2ST vendor-specific; use a conservative simulator floor.
        timing_dict.setdefault("nRCKSP2ST", max(8, timing_dict["nRCKEN"] + timing_dict["nRCK_LS"]))
        timing_dict.setdefault("nRCKST2SP", timing_dict["nRCKEN"] + timing_dict["nRCK_LS"] + timing_dict["nRCK_HS"])

        # RFM command-plumbing defaults. These are simulator placeholders, not JEDEC equality rules.
        timing_dict.setdefault("nRFMab", timing_dict["nRFCab"])
        timing_dict.setdefault("nRFMpb", timing_dict["nRFCpb"])

        for name in cls.timing_params:
            if name not in timing_dict:
                raise ValueError(f"GDDR7: timing {name} remains unresolved")


GDDR7.org_presets = {
    "GDDR7_16Gb_x8_4ch": {
        "density": 4096,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 14,
        "column": (1 << 6) << 5,
    },
    "GDDR7_32Gb_x8_4ch": {
        "density": 8192,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 15,
        "column": (1 << 6) << 5,
    },
    "GDDR7_64Gb_x8_4ch": {
        "density": 16384,
        "dq": 8,
        "channel_width": 8,
        "bank": 16,
        "row": 1 << 16,
        "column": (1 << 6) << 5,
    },
}


GDDR7.timing_presets = {
    # "GDDR7_TEST_28000_PAM3": {
    "GDDR7_TEST_28000": {
        # CI/smoke/regression only. This is not a vendor timing table.
        # The resolver fills fixed JESD239D values and GDDR6-derived guesstimates.
        # "nBL": 2,
        # nBL is intentionally absent: it is set from the `encoding` knob
        # (PAM3→2, NRZ→4) in resolve_secondary_timings.
        # Tuned smoke guesstimate from latency-throughput diagnostics; not source-backed GDDR7 vendor data.
        "nRTW": 12,
        "tCK_ps": 571,
    },
}
