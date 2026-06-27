import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _hbm3():
    return ramulator.dram.HBM3(org_preset="HBM3_8Gb_8hi", timing_preset="HBM3_6400Mbps")


def _hbm4_single_sid():
    return ramulator.dram.HBM4(org_preset="HBM4_32Gb_4Hi", timing_preset="HBM4_8000Mbps")


def _make_hbm34(dram=None):
    return cs.ControllerUnderTest.make_hbm34(
        dram or _hbm3(),
        refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
    )


def _collect_issued(dut, *, command, count, max_ticks):
    found = []
    for _ in range(max_ticks):
        for item in dut.tick():
            if item.command == command:
                found.append(item)
                if len(found) == count:
                    return found
    raise AssertionError(f"Did not observe {count} {command} commands in {max_ticks} ticks")


def _level_index(dut, name):
    return dut.level_names.index(name)


def _flat_bank(dut, item):
    return (
        item.addr_vec[_level_index(dut, "BankGroup")] * dut.org["bank"]
        + item.addr_vec[_level_index(dut, "Bank")]
    )


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4_single_sid])
def test_hbm34_per_bank_refresh_pairs_pcs_before_advancing_flat_bank(dram_factory):
    dut = _make_hbm34(dram_factory())

    refs = _collect_issued(dut, command="REFpb", count=4, max_ticks=2 * dut.timings["nREFIpb"] + 16)
    pc_idx = _level_index(dut, "PseudoChannel")
    sid_idx = _level_index(dut, "Sid")

    assert [item.addr_vec[pc_idx] for item in refs] == [0, 1, 0, 1]
    assert [item.addr_vec[sid_idx] for item in refs] == [0, 0, 0, 0]
    assert [_flat_bank(dut, item) for item in refs] == [0, 0, 1, 1]
    assert refs[1].clk - refs[0].clk == 2
    assert refs[3].clk - refs[2].clk == 2


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4_single_sid])
def test_hbm34_per_bank_refresh_visits_each_flat_bank_once_per_set(dram_factory):
    dut = _make_hbm34(dram_factory())
    banks_per_sid = dut.org["bankgroup"] * dut.org["bank"]
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=2 * banks_per_sid,
        max_ticks=(banks_per_sid + 1) * dut.timings["nREFIpb"] + 32,
    )

    pc_idx = _level_index(dut, "PseudoChannel")
    sid_idx = _level_index(dut, "Sid")
    seen = {0: [], 1: []}
    for item in refs:
        assert item.addr_vec[sid_idx] == 0
        seen[item.addr_vec[pc_idx]].append(_flat_bank(dut, item))

    assert seen[0] == list(range(banks_per_sid))
    assert seen[1] == list(range(banks_per_sid))


def test_hbm34_per_bank_refresh_waits_nrfc_before_repeating_set():
    dut = _make_hbm34(_hbm4_single_sid())
    banks_per_sid = dut.org["bankgroup"] * dut.org["bank"]
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=2 * banks_per_sid + 2,
        max_ticks=(banks_per_sid + 3) * dut.timings["nREFIpb"] + dut.timings["nRFCpb"] + 32,
    )

    pc_idx = _level_index(dut, "PseudoChannel")
    pc0_flat15 = next(
        item for item in refs if item.addr_vec[pc_idx] == 0 and _flat_bank(dut, item) == banks_per_sid - 1
    )
    pc1_flat15 = next(
        item for item in refs if item.addr_vec[pc_idx] == 1 and _flat_bank(dut, item) == banks_per_sid - 1
    )
    pc0_repeat = refs[2 * banks_per_sid]
    pc1_repeat = refs[2 * banks_per_sid + 1]

    assert pc0_repeat.addr_vec[pc_idx] == 0
    assert pc1_repeat.addr_vec[pc_idx] == 1
    assert _flat_bank(dut, pc0_repeat) == 0
    assert _flat_bank(dut, pc1_repeat) == 0
    assert pc0_repeat.clk - pc0_flat15.clk >= dut.timings["nRFCpb"]
    assert pc1_repeat.clk - pc1_flat15.clk >= dut.timings["nRFCpb"]
