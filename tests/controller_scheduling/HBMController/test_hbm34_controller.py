import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _hbm3():
    return ramulator.dram.HBM3(org_preset="HBM3_8Gb_8hi", timing_preset="HBM3_6400Mbps")


def _hbm4():
    return ramulator.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps")


def _make_hbm34(dram=None, *, refresh_manager=None):
    return cs.ControllerUnderTest.make_hbm34(
        dram or _hbm3(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.NoRefresh(),
    )


def _addr(dut, *, pc=0, sid=0, bankgroup=0, bank=0, row=0, column=0):
    levels = {"PseudoChannel": pc}
    if "Sid" in dut.level_names:
        levels["Sid"] = sid
    return dut.addr_vec(
        **levels,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=column,
    )


def _assert_one(issued, command):
    assert [item.command for item in issued] == [command]
    return issued[0]


def _pairing_gap(command):
    return 3 if command == "ACT" else 1


def _tick_to_pairing_edge(dut, rising_command, falling_command, rising_addr, falling_addr):
    dut.priority_send(rising_command, rising_addr)
    dut.priority_send(falling_command, falling_addr)

    first = _assert_one(dut.tick(), rising_command)
    for _ in range(_pairing_gap(rising_command) - 1):
        assert dut.tick() == []
    return first, dut.tick()


def test_hbm34_rising_only_row_command_waits_for_rising_edge():
    dut = _make_hbm34()
    a = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)

    assert dut.tick() == []
    dut.priority_send("REFpb", a)

    assert dut.tick() == []
    issued = dut.tick()

    rec = _assert_one(issued, "REFpb")
    assert rec.clk == 3


@pytest.mark.parametrize("command", ["PREpb", "PREab"])
def test_hbm34_falling_edge_pre_can_issue_with_rnop(command):
    dut = _make_hbm34()
    a = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)

    assert dut.tick() == []
    dut.priority_send(command, a)
    issued = dut.tick()

    rec = _assert_one(issued, command)
    assert rec.clk == 2


def test_hbm34_act_pairs_with_legal_falling_prepb_after_three_edge_ticks():
    dut = _make_hbm34()
    act = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)
    pre = _addr(dut, pc=0, sid=0, bankgroup=0, bank=1, row=0)

    dut.priority_send("ACT", act)
    dut.priority_send("PREpb", pre)

    _assert_one(dut.tick(), "ACT")
    assert dut.tick() == []
    assert dut.tick() == []
    issued = dut.tick()

    rec = _assert_one(issued, "PREpb")
    assert rec.clk == 4


def test_hbm34_act_rejects_same_bank_same_pc_falling_prepb_pair():
    dut = _make_hbm34()
    a = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)

    dut.priority_send("ACT", a)
    dut.priority_send("PREpb", a)

    _assert_one(dut.tick(), "ACT")
    assert dut.tick() == []
    assert dut.tick() == []
    assert dut.tick() == []


def test_hbm34_cross_pc_row_pre_pair_issues_on_adjacent_edges():
    dut = _make_hbm34()
    ref = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)
    pre = _addr(dut, pc=1, sid=0, bankgroup=0, bank=0)

    dut.priority_send("REFpb", ref)
    dut.priority_send("PREab", pre)

    first = _assert_one(dut.tick(), "REFpb")
    second = _assert_one(dut.tick(), "PREab")

    assert second.clk - first.clk == 1


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("falling_command", ["PREpb", "PREab"])
@pytest.mark.parametrize("rising_command", ["ACT", "PREpb", "PREab", "REFpb", "REFab", "RFMpb", "RFMab"])
def test_hbm34_table35_cross_pc_pre_options_issue(dram_factory, rising_command, falling_command):
    dut = _make_hbm34(dram_factory())
    rising = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)
    falling = _addr(dut, pc=1, sid=0, bankgroup=0, bank=0, row=0)

    first, issued = _tick_to_pairing_edge(dut, rising_command, falling_command, rising, falling)

    second = _assert_one(issued, falling_command)
    assert second.clk - first.clk == _pairing_gap(rising_command)


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("falling_command", ["PREpb", "PREab"])
@pytest.mark.parametrize("rising_command", ["PREab", "REFab", "RFMab"])
def test_hbm34_table35_same_pc_all_bank_rows_reject_pre(dram_factory, rising_command, falling_command):
    dut = _make_hbm34(dram_factory())
    rising = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)
    falling = _addr(dut, pc=0, sid=0, bankgroup=0, bank=1, row=0)

    _, issued = _tick_to_pairing_edge(dut, rising_command, falling_command, rising, falling)

    assert issued == []


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("rising_command", ["ACT", "PREpb", "REFpb", "RFMpb"])
def test_hbm34_table35_same_pc_bank_rows_reject_preab(dram_factory, rising_command):
    dut = _make_hbm34(dram_factory())
    rising = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)
    falling = _addr(dut, pc=0, sid=0, bankgroup=0, bank=1, row=0)

    _, issued = _tick_to_pairing_edge(dut, rising_command, "PREab", rising, falling)

    assert issued == []


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("rising_command", ["ACT", "PREpb", "REFpb", "RFMpb"])
def test_hbm34_table35_same_pc_bank_rows_reject_same_bank_prepb(dram_factory, rising_command):
    dut = _make_hbm34(dram_factory())
    addr = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)

    _, issued = _tick_to_pairing_edge(dut, rising_command, "PREpb", addr, addr)

    assert issued == []


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("rising_command", ["ACT", "REFpb", "RFMpb"])
def test_hbm34_table35_same_pc_bank_rows_allow_diff_bank_prepb_when_timing_allows(dram_factory, rising_command):
    dut = _make_hbm34(dram_factory())
    rising = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)
    falling = _addr(dut, pc=0, sid=0, bankgroup=0, bank=1, row=0)

    first, issued = _tick_to_pairing_edge(dut, rising_command, "PREpb", rising, falling)

    second = _assert_one(issued, "PREpb")
    assert second.clk - first.clk == _pairing_gap(rising_command)


def test_hbm34_column_command_only_issues_on_rising_edge():
    dut = _make_hbm34()
    a = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0, column=0)

    dut.priority_send("ACT", a)
    _assert_one(dut.tick(), "ACT")

    dut.priority_send("RD", a)
    for _ in range(100):
        issued = dut.tick()
        for rec in issued:
            if rec.command == "RD":
                assert rec.clk % 2 == 1
                return
    pytest.fail("RD was never issued")


def test_hbm34_falling_edge_pre_allowed_when_not_in_pair_window():
    dut = _make_hbm34()
    ref = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0)
    pre = _addr(dut, pc=0, sid=0, bankgroup=0, bank=1)

    dut.priority_send("REFpb", ref)
    _assert_one(dut.tick(), "REFpb")
    assert dut.tick() == []
    assert dut.tick() == []

    dut.priority_send("PREpb", pre)
    issued = dut.tick()

    rec = _assert_one(issued, "PREpb")
    assert rec.clk == 4
    assert rec.clk % 2 == 0


@pytest.mark.parametrize("dram_factory", [_hbm3, _hbm4])
@pytest.mark.parametrize("falling_command", ["PREpb", "PREab"])
@pytest.mark.parametrize("rising_command", ["PREab", "REFab", "RFMab"])
def test_hbm34_table35_same_pc_all_bank_rows_reject_pre_same_bank(dram_factory, rising_command, falling_command):
    dut = _make_hbm34(dram_factory())
    addr = _addr(dut, pc=0, sid=0, bankgroup=0, bank=0, row=0)

    _, issued = _tick_to_pairing_edge(dut, rising_command, falling_command, addr, addr)

    assert issued == []
