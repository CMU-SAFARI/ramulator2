import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


def make_dut(*, channel_id=0, **overrides):
    dram = ramulator.dram.HBM3(**{
        "org_preset": "HBM3_8Gb_8hi",
        "timing_preset": "HBM3_6400Mbps",
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def _addr(dut, *, sid, bankgroup, bank, row):
    return dut.addr_vec(
        PseudoChannel=0,
        Sid=sid,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=0,
    )


def _issue_two_acts_then_col(dut, command, a0, a1, *, act_gap_timing):
    dut.issue("ACT", a0, clk=0)
    dut.issue("ACT", a1, clk=dut.timings[act_gap_timing])
    rcd_timing = "nRCDRD" if command == "RD" else "nRCDWR"
    col_clk = max(
        dut.get_first_ready_clk(command, a0, dut.timings[rcd_timing]),
        dut.get_first_ready_clk(command, a1, dut.timings[rcd_timing]),
    )
    dut.issue(command, a0, clk=col_clk)
    return col_clk


def test_hbm3_same_sid_diff_bankgroup_column_spacing_uses_nccds():
    dut = make_dut()
    a0 = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a1 = _addr(dut, sid=0, bankgroup=1, bank=0, row=1)

    rd_clk = _issue_two_acts_then_col(dut, "RD", a0, a1, act_gap_timing="nRRDS")
    nccd = dut.timings["nCCDS"]

    dut.assert_earliest_ready_at("RD", a1, rd_clk + nccd)


def test_hbm3_diff_sid_column_spacing_uses_nccdr():
    dut = make_dut()
    a0 = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a1 = _addr(dut, sid=1, bankgroup=0, bank=0, row=1)

    rd_clk = _issue_two_acts_then_col(dut, "RD", a0, a1, act_gap_timing="nRRDS")
    nccd = dut.timings["nCCDR"]

    dut.assert_earliest_ready_at("RD", a1, rd_clk + nccd)


def test_hbm3_same_sid_same_bankgroup_column_spacing_uses_nccdl():
    dut = make_dut()
    a0 = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a1 = _addr(dut, sid=0, bankgroup=0, bank=1, row=1)

    rd_clk = _issue_two_acts_then_col(dut, "RD", a0, a1, act_gap_timing="nRRDL")
    nccd = dut.timings["nCCDL"]

    dut.assert_earliest_ready_at("RD", a1, rd_clk + nccd)


def test_hbm3_diff_sid_write_column_spacing_uses_nccds():
    dut = make_dut()
    a0 = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a1 = _addr(dut, sid=1, bankgroup=1, bank=0, row=1)

    wr_clk = _issue_two_acts_then_col(dut, "WR", a0, a1, act_gap_timing="nRRDS")
    nccd = dut.timings["nCCDS"]

    dut.assert_earliest_ready_at("WR", a1, wr_clk + nccd)
