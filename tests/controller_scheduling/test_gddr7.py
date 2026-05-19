import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _dram(**overrides):
    return ramulator.dram.GDDR7(
        org_preset="GDDR7_16Gb_x8_4ch",
        timing_preset="GDDR7_TEST_28000_PAM3",
        **overrides,
    )


def _make_gddr7(dram=None, *, refresh_manager=None, **kwargs):
    return cs.ControllerUnderTest.make_gddr7(
        dram or _dram(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.NoRefresh(),
        **kwargs,
    )


def _addr(dut, *, bank=0, row=0, column=0):
    return dut.addr_vec(Channel=0, Bank=bank, Row=row, Column=column)


def _all_bank_addr(dut):
    return dut.addr_vec(Channel=0, Bank=dut.ALL, Row=dut.ALL, Column=0)


def _level_index(dut, name):
    return dut.level_names.index(name)


def _collect_issued(dut, *, command, count, max_ticks):
    found = []
    for _ in range(max_ticks):
        for item in dut.tick():
            if item.command == command:
                found.append(item)
                if len(found) == count:
                    return found
    raise AssertionError(f"Did not observe {count} {command} commands in {max_ticks} ticks")


def test_gddr7_controller_rejects_non_always_on_rck_mode():
    with pytest.raises(RuntimeError, match="always_on"):
        _make_gddr7(rck_mode="explicit_start")


def test_gddr7_controller_dual_issues_row_and_column_when_both_are_ready():
    dut = _make_gddr7()
    open_bank = _addr(dut, bank=0, row=0)
    next_bank = _addr(dut, bank=1, row=1)

    dut.priority_send("ACT", open_bank)
    assert [item.command for item in dut.tick()] == ["ACT"]

    for _ in range(dut.timing("nRCDRD") - 1):
        assert dut.tick() == []

    dut.priority_send("RD", open_bank)
    dut.priority_send("ACT", next_bank)
    issued = dut.tick()

    assert [item.command for item in issued] == ["RD", "ACT"]
    assert issued[0].clk == issued[1].clk


def test_gddr7_all_bank_refresh_has_channel_scope_and_priority_over_rw():
    dut = _make_gddr7(
        _dram(nREFI=1),
        refresh_manager=ramulator.refresh_manager.AllBank(),
    )
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    issued = dut.tick()

    assert [item.command for item in issued] == ["REFab"]
    ref = issued[0]
    assert ref.addr_vec[_level_index(dut, "Channel")] == 0
    for name in ["Bank", "Row", "Column"]:
        assert ref.addr_vec[_level_index(dut, name)] == dut.ALL


def test_gddr7_per_bank_refresh_rotates_all_banks_and_pauses_before_repeating_set():
    dut = _make_gddr7(refresh_manager=ramulator.refresh_manager.PerBank())
    refs = _collect_issued(
        dut,
        command="REFpb",
        count=17,
        max_ticks=18 * dut.timing("nREFIpb") + dut.timing("nRFCpb") + 32,
    )

    bank_idx = _level_index(dut, "Bank")
    assert [item.addr_vec[bank_idx] for item in refs] == list(range(16)) + [0]
    assert refs[16].clk - refs[15].clk >= dut.timing("nRFCpb")


def test_gddr7_controller_does_not_emit_rfm_or_rck_commands_automatically():
    dut = _make_gddr7()
    dut.send_request("Read", _addr(dut, bank=0, row=0))
    history = dut.run_until_idle(max_ticks=128)

    assert [item.command for item in history] == ["ACT", "RD"]
    assert all(not item.command.startswith("RFM") for item in history)
    assert all(not item.command.startswith("RCK") for item in history)


def test_gddr7_manual_rfm_commands_use_device_command_plumbing():
    dut = _make_gddr7()

    dut.priority_send("RFMpb", _addr(dut, bank=0))
    dut.priority_send("RFMab", _all_bank_addr(dut))
    history = dut.run_until_idle(max_ticks=16)

    assert [item.command for item in history] == ["RFMpb", "RFMab"]


def test_gddr7_manual_rck_commands_follow_timing_model():
    dut = _make_gddr7()
    rck = _all_bank_addr(dut)

    dut.priority_send("RCKSTRT", rck)
    dut.priority_send("RCKSTOP", rck)
    history = dut.run_until_idle(max_ticks=64)

    assert [item.command for item in history] == ["RCKSTRT", "RCKSTOP"]
    assert history[1].clk - history[0].clk == dut.timing("nRCKST2SP")
