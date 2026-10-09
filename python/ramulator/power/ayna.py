"""``ramulator.power.ayna_power``: the ``power`` config of the Ayna controller plugin.

The device data is Ayna's (CMU-SAFARI/HBM-Power@318d3d1, BSD 3-Clause): per-pseudo-channel currents
with static data as the IDD4R/IDD4W reference.
"""

import copy

# Ayna's device characterization for each Ramulator DRAM standard. The plugin uses the entry for its
# controller's standard: fixed currents characterized at "rate", or, with "rate_scaling", Ayna's
# HBM3E rails scaled to the speedbin's data rate (config/HBM3E_6400MTs at 6400 MT/s and
# config/HBM4_8000MTs at 8000 MT/s).
STANDARDS = {
    # config/HBM2_1200MTs/power.json, and variation.json for the measured per-bank-group and
    # per-bank read and write current factors.
    "HBM2": {
        "model": "HBM2",
        "rate": 1200,
        "vDD": 1.2,
        "iDD0": 28.544,
        "iDD2N": 11.756,
        "iDD3N1": 12.394,
        "iDD3N16": 14.281,
        "iDD4R": 202.769,
        "iDD4W": 137.406,
        "iDD5B": 66.331,
        "variation": {
            "bankgroup_scaling_factors": [1.0391, 0.9603, 1.0411, 0.9595],
            "bank_scaling_factors": [1.0259, 1.0253, 0.9763, 0.9725],
            "bankgroup_write_scaling_factors": [1.0183, 0.9825, 1.0175, 0.9816],
            "bank_write_scaling_factors": [1.0028, 0.9998, 1.0006, 0.9969],
        },
    },
    # Ayna has no HBM3 characterization; HBM3 uses its HBM3E device (config/HBM3E_6400MTs).
    "HBM3": {"model": "HBM3", "vDD": 1.1, "vDDQ": 1.1, "rate_scaling": True},
    # config/HBM4_8000MTs: JESD270-4 VDDC (core) and VDDQ (I/O), on Ayna's HBM3 equations.
    "HBM4": {"model": "HBM3", "vDD": 1.05, "vDDQ": 0.9, "rate_scaling": True},
}

# Data-pattern model of Ayna's device configs (config/*/power.json, "datapattern"): the HBM2
# beat-pattern fit in its relative form, with uniform-random data (all knobs 0.5) and static data
# (all knobs 0) as the reference the IDD4R/IDD4W above are given at. The coupling terms, which the
# device configs leave off, carry the model's default coefficients (util/datapattern_model.h).
DATA_PATTERN = {
    "enabled": True,
    "floor_pJbit": 3.1798,
    "c_dq": 1.04,
    "c_t2bit": 1.4875,
    "c_busflip": 1.41,
    "use_coupling": False,
    "c_bg_coupling": 0.345,
    "c_tsv_coupling": 0.078,
    "bgcpl_full": 4.0,
    "tsvcpl_full": 4.0,
    "dq_rate": 0.5,
    "tsv_rate": 0.5,
    "bg_rate": 0.5,
    "ref_dq_rate": 0.0,
    "ref_tsv_rate": 0.0,
    "ref_bg_rate": 0.0,
    "K": 0.0,
    "apply_to_writes": True,
}

ELECTRICAL_FIELDS = ("vDD", "vDDQ", "iDD0", "iDD2N", "iDD3N1", "iDD3N16", "iDD4R", "iDD4W", "iDD5B")


def ayna_power(*, data_pattern=None, variation=False, **overrides):
    """The ``power`` config of ``ramulator.controller_plugin.Ayna``.

    The plugin reads the DRAM standard and data rate from its controller and picks the matching
    characterization.

    Args:
        data_pattern: a ``DataPattern`` with the data-activity knobs (default: uniform-random data).
        variation: apply Ayna's measured per-bank-group and per-bank factors (HBM2 only).
        **overrides: electrical fields (e.g. ``iDD4R=...``) or data-pattern fields to override.
    """
    data = copy.deepcopy(DATA_PATTERN)
    if data_pattern is not None:
        data.update(data_pattern.knobs())
    electrical = {}
    for key, value in overrides.items():
        if key in ELECTRICAL_FIELDS:
            electrical[key] = value
        elif key in data:
            data[key] = value
        else:
            raise ValueError(f"Unknown Ayna power field '{key}'")
    return {
        "standards": copy.deepcopy(STANDARDS),
        "dataPattern": data,
        "variation": variation,
        "overrides": electrical,
    }
