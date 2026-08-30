import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _hbm3():
    return ramulator.dram.HBM3(org_preset="HBM3_16Gb_8hi", timing_preset="HBM3_6400Mbps")


def _hbm1():
    return ramulator.dram.HBM1(org_preset="HBM1_4Gb", timing_preset="HBM1_2Gbps")


def _hbm2_stacked():
    return ramulator.dram.HBM2(org_preset="HBM2_8Gb", timing_preset="HBM2_2000Mbps")


def _hbm4_single_sid():
    return ramulator.dram.HBM4(org_preset="HBM4_32Gb_4Hi", timing_preset="HBM4_8000Mbps")


def _make_hbm34(dram=None, refresh_manager=None):
    return cs.ControllerUnderTest.make_hbm34(
        dram or _hbm3(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.HBMPerBankRefresh(),
    )


def _make_hbm2(dram=None, refresh_manager=None):
    return cs.ControllerUnderTest.make_hbm12(
        dram or _hbm2_stacked(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.HBMPerBankRefresh(),
    )


def _make_hbm1(dram=None, refresh_manager=None):
    return cs.ControllerUnderTest.make_hbm12(
        dram or _hbm1(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.HBMPerBankRefresh(),
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


def test_hbm34_per_bank_refresh_observes_sid_boundaries_without_cadence_drift():
    dut = _make_hbm34(_hbm3())
    banks_per_sid = dut.org["bankgroup"] * dut.org["bank"]
    banks_per_pc = dut.org["sid"] * banks_per_sid
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=2 * (2 * banks_per_pc + 1),
        max_ticks=(2 * banks_per_pc + 3) * dut.timings["nREFIpb"]
        + 4 * dut.timings["nRFCpb"]
        + 64,
    )
    pc_idx = _level_index(dut, "PseudoChannel")

    for pc in range(dut.org["pseudochannel"]):
        pc_refs = [item for item in refs if item.addr_vec[pc_idx] == pc]
        assert pc_refs[banks_per_sid].clk - pc_refs[banks_per_sid - 1].clk >= dut.timings["nRFCpb"]
        assert pc_refs[banks_per_pc].clk - pc_refs[banks_per_pc - 1].clk >= dut.timings["nRFCpb"]
        assert (
            pc_refs[2 * banks_per_pc].clk - pc_refs[banks_per_pc].clk
            == banks_per_pc * dut.timings["nREFIpb"]
        )


def test_hbm2_per_bank_refresh_pairs_pseudochannels():
    dut = _make_hbm2()
    refs = _collect_issued(dut, command="REFpb", count=4, max_ticks=2 * dut.timings["nREFIpb"] + 16)
    pc_idx = _level_index(dut, "PseudoChannel")
    sid_idx = _level_index(dut, "Sid")

    assert [item.addr_vec[pc_idx] for item in refs] == [0, 1, 0, 1]
    assert [item.addr_vec[sid_idx] for item in refs] == [0, 0, 0, 0]
    assert [_flat_bank(dut, item) for item in refs] == [0, 0, 1, 1]
    assert refs[1].clk - refs[0].clk == 1
    assert refs[3].clk - refs[2].clk == 1


def test_hbm2_per_bank_refresh_waits_nrfc_after_the_complete_bank_set():
    dut = _make_hbm2()
    banks_per_pc = dut.org["sid"] * dut.org["bankgroup"] * dut.org["bank"]
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=2 * banks_per_pc + 2,
        max_ticks=(banks_per_pc + 3) * dut.timings["nREFIpb"] + dut.timings["nRFCpb"] + 32,
    )
    pc_idx = _level_index(dut, "PseudoChannel")
    sid_idx = _level_index(dut, "Sid")

    for pc in range(dut.org["pseudochannel"]):
        sid0_last = next(
            item
            for item in refs
            if item.addr_vec[pc_idx] == pc
            and item.addr_vec[sid_idx] == 0
            and _flat_bank(dut, item) == dut.org["bankgroup"] * dut.org["bank"] - 1
        )
        sid1_first = next(
            item
            for item in refs
            if item.addr_vec[pc_idx] == pc
            and item.addr_vec[sid_idx] == 1
            and _flat_bank(dut, item) == 0
        )
        assert sid1_first.clk - sid0_last.clk == dut.timings["nREFIpb"]

    pc0_last = next(
        item
        for item in refs
        if item.addr_vec[pc_idx] == 0
        and item.addr_vec[sid_idx] == dut.org["sid"] - 1
        and _flat_bank(dut, item) == dut.org["bankgroup"] * dut.org["bank"] - 1
    )
    pc1_last = next(
        item
        for item in refs
        if item.addr_vec[pc_idx] == 1
        and item.addr_vec[sid_idx] == dut.org["sid"] - 1
        and _flat_bank(dut, item) == dut.org["bankgroup"] * dut.org["bank"] - 1
    )
    pc0_repeat = refs[2 * banks_per_pc]
    pc1_repeat = refs[2 * banks_per_pc + 1]

    assert pc0_repeat.addr_vec[pc_idx] == 0
    assert pc1_repeat.addr_vec[pc_idx] == 1
    assert pc0_repeat.addr_vec[sid_idx] == 0
    assert pc1_repeat.addr_vec[sid_idx] == 0
    assert pc0_repeat.clk - pc0_last.clk >= dut.timings["nRFCpb"]
    assert pc1_repeat.clk - pc1_last.clk >= dut.timings["nRFCpb"]


def test_hbm1_per_bank_refresh_observes_the_set_boundary():
    dram = ramulator.dram.HBM1(
        org_preset="HBM1_4Gb",
        timing_preset="HBM1_2Gbps",
        nREFIpb=2,
        nRFCpb=10,
        nRREFD=1,
        nFAW=1,
    )
    dut = _make_hbm1(dram)
    bank_count = dut.org["bankgroup"] * dut.org["bank"]
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=bank_count + 1,
        max_ticks=bank_count * dut.timings["nREFIpb"] + dut.timings["nRFCpb"] + 16,
    )

    assert [_flat_bank(dut, item) for item in refs[:-1]] == list(range(bank_count))
    assert _flat_bank(dut, refs[-1]) == 0
    assert refs[-1].clk - refs[-2].clk >= dut.timings["nRFCpb"]


def test_hbm1_per_bank_refresh_anchors_boundary_to_refpb_issue():
    dram = ramulator.dram.HBM1(
        org_preset="HBM1_4Gb",
        timing_preset="HBM1_2Gbps",
        nREFIpb=2,
        nRFCpb=10,
        nRREFD=1,
        nFAW=1,
    )
    dut = _make_hbm1(dram)
    bank_count = dut.org["bankgroup"] * dut.org["bank"]
    last_bank = dut.addr_vec(
        Channel=0,
        BankGroup=dut.org["bankgroup"] - 1,
        Bank=dut.org["bank"] - 1,
        Row=0,
    )
    dut.priority_send("ACT", last_bank)

    refs = _collect_issued(
        dut,
        command="REFpb",
        count=bank_count + 1,
        max_ticks=bank_count * dut.timings["nREFIpb"]
        + dut.timings["nRC"]
        + dut.timings["nRFCpb"]
        + 32,
    )
    boundary_ref = refs[-2]
    next_set_ref = refs[-1]
    boundary_pre = next(
        item
        for item in dut.history
        if item.command == "PREpb" and _flat_bank(dut, item) == bank_count - 1
    )

    assert boundary_pre.clk < boundary_ref.clk
    assert _flat_bank(dut, boundary_ref) == bank_count - 1
    assert _flat_bank(dut, next_set_ref) == 0
    assert next_set_ref.clk - boundary_ref.clk >= dut.timings["nRFCpb"]


def test_hbm34_per_bank_refresh_compatibility_name_remains_available():
    dut = _make_hbm34(
        _hbm4_single_sid(),
        refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
    )

    assert dut.controller.refresh_manager.impl == "HBM34PerBankRefresh"
    refs = _collect_issued(dut, command="REFpb", count=2, max_ticks=dut.timings["nREFIpb"] + 16)
    assert len(refs) == 2


def test_generic_per_bank_refresh_rejects_hbm_standards():
    with pytest.raises(RuntimeError, match="use HBMPerBankRefresh"):
        _make_hbm1(refresh_manager=ramulator.refresh_manager.PerBank())
    with pytest.raises(RuntimeError, match="use HBMPerBankRefresh"):
        _make_hbm2(refresh_manager=ramulator.refresh_manager.PerBank())
