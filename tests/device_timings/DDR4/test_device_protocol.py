import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


def make_dut():
    dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
    return device_timings.DeviceUnderTest(dram)


def test_rd_on_closed_bank_requires_act():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

    probe = dut.probe("RD", a, clk=0)

    assert probe.preq == "ACT"
    assert probe.timing_OK is True
    assert probe.ready is False
    assert probe.row_hit is False
    assert probe.row_open is False


def test_act_rd_gap_respects_nrcd():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

    dut.issue("ACT", a, clk=0)

    early = dut.probe("RD", a, clk=dut.timings["nRCD"] - 1)
    ontime = dut.probe("RD", a, clk=dut.timings["nRCD"])

    assert early.preq == "RD"
    assert early.timing_OK is False
    assert early.ready is False
    assert early.row_hit is True
    assert early.row_open is True

    assert ontime.timing_OK is True
    assert ontime.ready is True


def test_rd_to_prepb_gap_respects_nrtp():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

    dut.issue("ACT", a, clk=0)
    dut.issue("RD", a, clk=dut.timings["nRCD"])

    prepb_clk = max(dut.timings["nRCD"] + dut.timings["nRTP"], dut.timings["nRAS"])
    early = dut.probe("PREpb", a, clk=prepb_clk - 1)
    ontime = dut.probe("PREpb", a, clk=prepb_clk)

    assert early.preq == "PREpb"
    assert early.timing_OK is False
    assert ontime.ready is True


def test_prepb_to_act_gap_respects_nrp():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

    dut.issue("ACT", a, clk=0)
    dut.issue("RD", a, clk=dut.timings["nRCD"])
    prepb_clk = max(dut.timings["nRCD"] + dut.timings["nRTP"], dut.timings["nRAS"])
    dut.issue("PREpb", a, clk=prepb_clk)

    early = dut.probe("ACT", a, clk=prepb_clk + dut.timings["nRP"] - 1)
    ontime = dut.probe("ACT", a, clk=prepb_clk + dut.timings["nRP"])

    assert early.timing_OK is False
    assert ontime.ready is True


def test_refab_requires_preab_when_bank_is_open():
    dut = make_dut()
    open_bank = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)
    ref_addr = dut.addr_vec(Rank=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=0)

    dut.issue("ACT", open_bank, clk=0)
    probe = dut.probe("REFab", ref_addr, clk=1)

    assert probe.preq == "PREab"
    assert probe.ready is False


def test_fifth_activate_within_nfaw_is_blocked():
    dut = make_dut()
    act_gap = dut.timings["nRRDS"]
    banks = [
        dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0),
        dut.addr_vec(Rank=0, BankGroup=1, Bank=0, Row=1, Column=0),
        dut.addr_vec(Rank=0, BankGroup=2, Bank=0, Row=2, Column=0),
        dut.addr_vec(Rank=0, BankGroup=3, Bank=0, Row=3, Column=0),
        dut.addr_vec(Rank=0, BankGroup=0, Bank=1, Row=4, Column=0),
    ]

    for i in range(4):
        dut.issue("ACT", banks[i], clk=i * act_gap)

    blocked = dut.probe("ACT", banks[4], clk=4 * act_gap)
    allowed = dut.probe("ACT", banks[4], clk=dut.timings["nFAW"])

    assert blocked.timing_OK is False
    assert blocked.ready is False
    assert allowed.ready is True


def test_ddr4_bankgroup_spacing_differs_between_same_and_different_groups():
    same_group = make_dut()
    a0 = same_group.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    a1 = same_group.addr_vec(Rank=0, BankGroup=0, Bank=1, Row=1, Column=0)
    same_group.issue("ACT", a0, clk=0)

    assert same_group.probe("ACT", a1, clk=same_group.timings["nRRDL"] - 1).timing_OK is False
    assert same_group.probe("ACT", a1, clk=same_group.timings["nRRDL"]).ready is True

    diff_group = make_dut()
    b0 = diff_group.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    b1 = diff_group.addr_vec(Rank=0, BankGroup=1, Bank=0, Row=1, Column=0)
    diff_group.issue("ACT", b0, clk=0)

    assert diff_group.probe("ACT", b1, clk=diff_group.timings["nRRDS"] - 1).timing_OK is False
    assert diff_group.probe("ACT", b1, clk=diff_group.timings["nRRDS"]).ready is True


def test_addr_vec_rejects_unknown_levels_and_none():
    dut = make_dut()

    with pytest.raises(ValueError, match="Unknown addr_vec levels"):
        dut.addr_vec(NotALevel=0)

    with pytest.raises(TypeError, match="expects an int"):
        dut.addr_vec(Rank=None)


def test_addr_vec_accepts_dut_all_wildcards():
    dut = make_dut()

    assert dut.addr_vec(Rank=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=0) == [
        0,
        0,
        -1,
        -1,
        -1,
        0,
    ]
