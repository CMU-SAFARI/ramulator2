import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _hbm3():
    return ramulator.dram.HBM3(org_preset="HBM3_8Gb_8hi", timing_preset="HBM3_6400Mbps")


def _hbm4():
    return ramulator.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps")


def _make_hbm34(dram):
    return cs.ControllerUnderTest.make_hbm34(dram)


def _addr(dut, *, pc=0, sid=0, bankgroup=0, bank=0, row=0, column=0):
    return dut.addr_vec(
        PseudoChannel=pc,
        Sid=sid,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=column,
    )


def _assert_one(issued, command):
    assert [item.command for item in issued] == [command]
    return issued[0]


@pytest.mark.parametrize(
    "first,second",
    [
        ("PREpb", "PREpb"),
        ("PREpb", "PREab"),
        ("PREab", "PREpb"),
        ("PREab", "PREab"),
    ],
)
@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
def test_hbm34_same_pc_pre_combinations_wait_for_nppd(dram_factory, first, second):
    dut = _make_hbm34(dram_factory())
    first_addr = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)
    second_addr = _addr(dut, pc=0, sid=0, bankgroup=1, bank=0)

    dut.priority_send(first, first_addr)
    dut.priority_send(second, second_addr)
    history = dut.run_until_idle(max_ticks=64)

    assert [item.command for item in history] == [first, second]
    assert history[1].clk - history[0].clk == dut.timings["nPPD"]


@pytest.mark.parametrize(
    "first,second",
    [
        ("PREpb", "PREpb"),
        ("PREpb", "PREab"),
        ("PREab", "PREpb"),
        ("PREab", "PREab"),
    ],
)
@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
def test_hbm34_cross_pc_pre_pairs_can_issue_on_adjacent_edges(dram_factory, first, second):
    dut = _make_hbm34(dram_factory())
    first_addr = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)
    second_addr = _addr(dut, pc=1, sid=0, bankgroup=0, bank=0)

    dut.priority_send(first, first_addr)
    dut.priority_send(second, second_addr)

    first_issue = _assert_one(dut.tick(), first)
    second_issue = _assert_one(dut.tick(), second)
    assert second_issue.clk - first_issue.clk == 1


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
def test_hbm34_same_pc_refpb_waits_for_nrrefd_but_cross_pc_does_not(dram_factory):
    same_pc = _make_hbm34(dram_factory())
    a0 = _addr(same_pc, pc=0, sid=0, bankgroup=0, bank=0)
    a1 = _addr(same_pc, pc=0, sid=0, bankgroup=0, bank=1)
    same_pc.priority_send("REFpb", a0)
    same_pc.priority_send("REFpb", a1)
    same_pc_history = same_pc.run_until_idle(max_ticks=64)

    assert [item.command for item in same_pc_history] == ["REFpb", "REFpb"]
    assert same_pc_history[1].clk - same_pc_history[0].clk == same_pc.timings["nRREFD"]

    cross_pc = _make_hbm34(dram_factory())
    b0 = _addr(cross_pc, pc=0, sid=0, bankgroup=0, bank=0)
    b1 = _addr(cross_pc, pc=1, sid=0, bankgroup=0, bank=0)
    cross_pc.priority_send("REFpb", b0)
    cross_pc.priority_send("REFpb", b1)
    cross_pc_history = cross_pc.run_until_idle(max_ticks=16)

    assert [item.command for item in cross_pc_history] == ["REFpb", "REFpb"]
    assert cross_pc_history[1].clk - cross_pc_history[0].clk == 2
