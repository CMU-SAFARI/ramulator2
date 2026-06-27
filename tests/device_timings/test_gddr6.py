import pytest

import ramulator
import tests.device_timings.harness as device_timings

pytestmark = pytest.mark.device_timings

TIMING_PRESET = "GDDR6_14000_1250mV_double"


def make_dut(*, channel_id=0, **overrides):
    """GDDR6 8Gb x16 @ 14Gbps (1250mV, double). Override via kwargs (e.g. nFAW=60)."""
    dram = ramulator.dram.GDDR6(**{
        "org_preset": "GDDR6_8Gb_x16",
        "timing_preset": TIMING_PRESET,
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def addr(dut, *, bankgroup=0, bank=0, row=0, column=0):
    return dut.addr_vec(
        Channel=0,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=column,
    )


def test_rd_on_closed_bank_requires_act():
    dut = make_dut()
    a = addr(dut)

    probe = dut.probe("RD", a, clk=0)

    assert probe.preq == "ACT"
    assert probe.timing_OK is True
    assert probe.ready is False
    assert probe.row_hit is False
    assert probe.row_open is False


def test_basic_read_and_write_sequences_respect_precharge_constraints():
    read_dut = make_dut()
    a = addr(read_dut)

    t_act = 0
    read_dut.assert_earliest_ready_at("ACT", a, t_act)
    read_dut.issue("ACT", a, t_act)

    t_rd = t_act + read_dut.timings["nRCDRD"]
    read_dut.assert_earliest_ready_at("RD", a, t_rd)
    read_dut.issue("RD", a, t_rd)

    t_pre = max(t_rd + read_dut.timings["nRTP"], t_act + read_dut.timings["nRAS"])
    read_dut.assert_earliest_ready_at("PREpb", a, t_pre)
    read_dut.issue("PREpb", a, t_pre)

    write_dut = make_dut()
    b = addr(write_dut)

    t_act = 0
    write_dut.assert_earliest_ready_at("ACT", b, t_act)
    write_dut.issue("ACT", b, t_act)

    t_wr = t_act + write_dut.timings["nRCDWR"]
    write_dut.assert_earliest_ready_at("WR", b, t_wr)
    write_dut.issue("WR", b, t_wr)

    t_pre = max(
        t_wr + write_dut.timings["nCWL"] + write_dut.timings["nBL"] + write_dut.timings["nWR"],
        t_act + write_dut.timings["nRAS"],
    )
    write_dut.assert_earliest_ready_at("PREpb", b, t_pre)
    write_dut.issue("PREpb", b, t_pre)


def test_gddr6_bankgroup_cas_spacing_uses_short_and_long_delays():
    same_group = make_dut()
    same_a0 = addr(same_group, bankgroup=0, bank=0, row=0)
    same_a1 = addr(same_group, bankgroup=0, bank=1, row=1)
    same_group.issue("ACT", same_a0, clk=0)
    same_group.issue("ACT", same_a1, clk=same_group.timings["nRRDL"])
    rd_clk = same_group.timings["nRRDL"] + same_group.timings["nRCDRD"]
    same_group.issue("RD", same_a0, clk=rd_clk)
    same_group.assert_earliest_ready_at("RD", same_a1, rd_clk + same_group.timings["nCCDL"])

    diff_group = make_dut()
    diff_a0 = addr(diff_group, bankgroup=0, bank=0, row=0)
    diff_a1 = addr(diff_group, bankgroup=1, bank=0, row=1)
    diff_group.issue("ACT", diff_a0, clk=0)
    diff_group.issue("ACT", diff_a1, clk=diff_group.timings["nRRDS"])
    rd_clk = diff_group.timings["nRRDS"] + diff_group.timings["nRCDRD"]
    diff_group.issue("RD", diff_a0, clk=rd_clk)
    diff_group.assert_earliest_ready_at("RD", diff_a1, rd_clk + diff_group.timings["nCCDS"])


def test_gddr6_read_write_turnarounds_respect_direction_and_bankgroup():
    read_to_write_dut = make_dut()
    a0 = addr(read_to_write_dut, bankgroup=0, bank=0, row=0)
    a1 = addr(read_to_write_dut, bankgroup=1, bank=0, row=1)

    t_act0 = 0
    read_to_write_dut.assert_earliest_ready_at("ACT", a0, t_act0)
    read_to_write_dut.issue("ACT", a0, t_act0)

    t_act1 = t_act0 + read_to_write_dut.timings["nRRDS"]
    read_to_write_dut.assert_earliest_ready_at("ACT", a1, t_act1)
    read_to_write_dut.issue("ACT", a1, t_act1)

    t_rd = t_act0 + read_to_write_dut.timings["nRCDRD"]
    read_to_write_dut.assert_earliest_ready_at("RD", a0, t_rd)
    read_to_write_dut.issue("RD", a0, t_rd)

    t_wr = max(
        t_act1 + read_to_write_dut.timings["nRCDWR"],
        t_rd + read_to_write_dut.timings["nCL"] + 1,
    )
    read_to_write_dut.assert_earliest_ready_at("WR", a1, t_wr)
    read_to_write_dut.issue("WR", a1, t_wr)

    write_to_read_dut = make_dut()
    b0 = addr(write_to_read_dut, bankgroup=0, bank=0, row=0)
    b1 = addr(write_to_read_dut, bankgroup=1, bank=0, row=1)

    t_act0 = 0
    write_to_read_dut.assert_earliest_ready_at("ACT", b0, t_act0)
    write_to_read_dut.issue("ACT", b0, t_act0)

    t_act1 = t_act0 + write_to_read_dut.timings["nRRDS"]
    write_to_read_dut.assert_earliest_ready_at("ACT", b1, t_act1)
    write_to_read_dut.issue("ACT", b1, t_act1)

    t_wr = t_act0 + write_to_read_dut.timings["nRCDWR"]
    write_to_read_dut.assert_earliest_ready_at("WR", b0, t_wr)
    write_to_read_dut.issue("WR", b0, t_wr)

    t_rd = max(
        t_act1 + write_to_read_dut.timings["nRCDRD"],
        t_wr
        + write_to_read_dut.timings["nCWL"]
        + write_to_read_dut.timings["nBL"]
        + write_to_read_dut.timings["nWTRS"],
    )
    write_to_read_dut.assert_earliest_ready_at("RD", b1, t_rd)
    write_to_read_dut.issue("RD", b1, t_rd)


def test_gddr6_activate_window_blocks_fifth_activate():
    dut = make_dut(nFAW=60)
    banks = [
        addr(dut, bankgroup=0, bank=0, row=0),
        addr(dut, bankgroup=1, bank=0, row=1),
        addr(dut, bankgroup=2, bank=0, row=2),
        addr(dut, bankgroup=3, bank=0, row=3),
        addr(dut, bankgroup=0, bank=1, row=4),
    ]

    for idx in range(4):
        dut.issue("ACT", banks[idx], clk=idx * dut.timings["nRRDS"])

    dut.assert_earliest_ready_at("ACT", banks[4], dut.timings["nFAW"])


def test_gddr6_refresh_delays_same_and_different_banks():
    same_bank = make_dut()
    a0 = addr(same_bank, bankgroup=0, bank=0)
    t_ref = 0
    same_bank.assert_earliest_ready_at("REFpb", a0, t_ref)
    same_bank.issue("REFpb", a0, t_ref)
    t_act = t_ref + same_bank.timings["nRFCpb"]
    same_bank.assert_earliest_ready_at("ACT", a0, t_act)
    same_bank.issue("ACT", a0, t_act)

    different_bank = make_dut()
    b0 = addr(different_bank, bankgroup=0, bank=0)
    b1 = addr(different_bank, bankgroup=1, bank=0)
    t_ref = 0
    different_bank.assert_earliest_ready_at("REFpb", b0, t_ref)
    different_bank.issue("REFpb", b0, t_ref)
    t_act = t_ref + different_bank.timings["nRREFD"]
    different_bank.assert_earliest_ready_at("ACT", b1, t_act)
    different_bank.issue("ACT", b1, t_act)

    all_bank = make_dut()
    ref_addr = all_bank.addr_vec(
        Channel=0,
        BankGroup=all_bank.ALL,
        Bank=all_bank.ALL,
        Row=all_bank.ALL,
        Column=0,
    )
    c1 = addr(all_bank, bankgroup=1, bank=0)
    t_ref = 0
    all_bank.assert_earliest_ready_at("REFab", ref_addr, t_ref)
    all_bank.issue("REFab", ref_addr, t_ref)
    t_act = t_ref + all_bank.timings["nRFCab"]
    all_bank.assert_earliest_ready_at("ACT", c1, t_act)
    all_bank.issue("ACT", c1, t_act)
