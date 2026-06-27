'''
checks that the newly added timing values are mapped correctly 
and hold the right values according to JESD209-5B (LPDDR5)
run with: PYTHONPATH="$PWD/python:$PWD/tests" python3 -m pytest -s -q tests/test_LPDDR5_params.py
'''

import math
import pytest
import ramulator
from ramulator._ramulator_test import _DeviceUnderTest


OLD_BURST_TIMING = "n" + "BL"


def test_lpddr5_8gb_6400_timing_parameters():
    import ramulator
    from ramulator._ramulator_test import _DeviceUnderTest

    lpddr5 = ramulator.dram.LPDDR5(
        org_preset="LPDDR5_8Gb_x16",
        timing_preset="LPDDR5_6400",
        rank=1,
    )

    cfg = lpddr5.to_config()
    timing_params = ramulator.dram.LPDDR5.timing_params

    timing_by_name = {
        name: cfg["timing"][idx]
        for idx, name in enumerate(timing_params)
    }
    assert OLD_BURST_TIMING not in timing_params

    expected = {
        "nBL_min": 2,
        "nBL_max": 4,
        "nRFC": 168,       # JESD209-5B Table 235: tRFCab = 210 ns / 1.25 ns.
        "nRFCpb": 96,      # 120 ns / 1.25 ns
        "nPBR2PBR": 72,    # 90 ns / 1.25 ns
        "nPBR2ACT": 6,     # 7.5 ns / 1.25 ns
    }

    for name, value in expected.items():
        assert timing_by_name[name] == value, (
            f"Python to_config timing mismatch for {name}: "
            f"expected {value}, got {timing_by_name[name]}. "
            f"timing_params={timing_params}, timing={cfg['timing']}"
        )

    # also check in test harness
    dut = _DeviceUnderTest(cfg)
    assert OLD_BURST_TIMING not in dut.timings
    for name, value in expected.items():
        assert dut.timing(name) == value, (
            f"C++ DUT timing mismatch for {name}: "
            f"expected {value}, got {dut.timing(name)}. "
            f"dut.timings={dut.timings}"
        )

    print(f"PASS: LPDDR5 {lpddr5.org_preset} {lpddr5.timing_preset}\ntiming parameters are mapped correctly in Python config and C++ DUT.")


def expected_nRFC(density, tCK_ps):
    # JESD209-5B Table 235: tRFCab.
    if density <= 2048:
        ns = 130
    elif density <= 4096:
        ns = 180
    elif density <= 8192:
        ns = 210
    elif density <= 16384:
        ns = 280
    else:
        ns = 380
    return math.ceil(ns * 1000 / tCK_ps)


def expected_nRFCpb(density, tCK_ps):
    # JESD209-5B Table 235: tRFCpb.
    if density <= 2048:
        ns = 60
    elif density <= 4096:
        ns = 90
    elif density <= 8192:
        ns = 120
    elif density <= 16384:
        ns = 140
    else:
        ns = 190
    return math.ceil(ns * 1000 / tCK_ps)


def expected_nPBR2PBR(density, tCK_ps):
    # JESD209-5B Table 235: tpbR2pbR.
    ns = 60 if density <= 2048 else 90
    return math.ceil(ns * 1000 / tCK_ps)


def expected_nPBR2ACT(tCK_ps):
    # JESD209-5B Table 235: tpbR2act.
    return math.ceil(7500 / tCK_ps)


@pytest.mark.parametrize("org_preset", sorted(ramulator.dram.LPDDR5.org_presets.keys()))
@pytest.mark.parametrize("timing_preset", sorted(ramulator.dram.LPDDR5.timing_presets.keys()))
def test_lpddr5_timing_mapping_for_all_org_and_timing_presets(org_preset, timing_preset):
    lpddr5 = ramulator.dram.LPDDR5(
        org_preset=org_preset,
        timing_preset=timing_preset,
        rank=1,
    )

    cfg = lpddr5.to_config()
    timing_params = ramulator.dram.LPDDR5.timing_params
    timing_by_name = {
        name: cfg["timing"][idx]
        for idx, name in enumerate(timing_params)
    }

    density = ramulator.dram.LPDDR5.org_presets[org_preset]["density"]
    tCK_ps = timing_by_name["tCK_ps"]

    expected = {
        "nBL_min": 2,
        "nBL_max": 4,
        "nRFC": expected_nRFC(density, tCK_ps),
        "nRFCpb": expected_nRFCpb(density, tCK_ps),
        "nPBR2PBR": expected_nPBR2PBR(density, tCK_ps),
        "nPBR2ACT": expected_nPBR2ACT(tCK_ps),
    }

    for name, value in expected.items():
        assert timing_by_name[name] == value, (
            f"Python to_config mismatch for {org_preset}/{timing_preset}: "
            f"{name}: expected {value}, got {timing_by_name[name]}. "
            f"timing_params={timing_params}, timing={cfg['timing']}"
        )


    dut = _DeviceUnderTest(cfg)
    for name, value in expected.items():
        assert dut.timing(name) == value, (
            f"C++ DUT mismatch for {org_preset}/{timing_preset}: "
            f"{name}: expected {value}, got {dut.timing(name)}. "
            f"dut.timings={dut.timings}"
        )

    print(f"PASS: timing mapping OK for {org_preset} / {timing_preset}")
