import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _dram(**overrides):
    return ramulator.dram.GDDR7(
        org_preset="GDDR7_16Gb_x8_4ch",
        # timing_preset="GDDR7_TEST_28000_PAM3",
        timing_preset="GDDR7_TEST_28000",
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


# def test_gddr7_controller_rejects_non_always_on_rck_mode():
#     with pytest.raises(RuntimeError, match="always_on"):
#         _make_gddr7(rck_mode="explicit_start")

def test_gddr7_controller_rejects_unknown_rck_mode():
    with pytest.raises(RuntimeError, match="not supported"):
        _make_gddr7(rck_mode="not_a_real_mode")


@pytest.mark.parametrize("mode", ["disabled", "always_on"])
def test_gddr7_controller_rejects_manual_rck_commands_in_inactive_modes(mode):
    dut = _make_gddr7(rck_mode=mode)
    rck = _all_bank_addr(dut)
    with pytest.raises(RuntimeError, match="illegal"):
        dut.priority_send("RCKSTRT", rck)
    with pytest.raises(RuntimeError, match="illegal"):
        dut.priority_send("RCKSTOP", rck)


def test_gddr7_controller_rejects_rckstrt_in_start_with_read_mode():
    dut = _make_gddr7(rck_mode="start_with_read")
    with pytest.raises(RuntimeError, match="illegal"):
        dut.priority_send("RCKSTRT", _all_bank_addr(dut))


def test_gddr7_start_with_read_never_emits_rckstrt_and_stops_after_idle():
    idle = 8
    dut = _make_gddr7(rck_mode="start_with_read", rck_idle_threshold=idle)
    dut.send_request("Read", _addr(dut, bank=0, row=0))
    history = dut.run_until_idle(
        max_ticks=idle + dut.timing("nRD2RCKSTOP") + dut.timing("nRCKSTOP_LAT") + 64,
    )

    cmds = [item.command for item in history]
    assert "RCKSTRT" not in cmds
    assert cmds.count("RCKSTOP") == 1

    last_rd_clk = max(item.clk for item in history if item.command in ("RD", "RDA"))
    rckstop_clk = next(item.clk for item in history if item.command == "RCKSTOP")
    assert rckstop_clk - last_rd_clk >= dut.timing("nRD2RCKSTOP")


def test_gddr7_start_with_rckstrt_emits_rckstrt_before_first_read():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    dut.send_request("Read", _addr(dut, bank=0, row=0))
    history = dut.run_until_idle(max_ticks=128)

    cmds = [item.command for item in history]
    rckstrt_idx = cmds.index("RCKSTRT")
    first_rd_idx = cmds.index("RD")
    assert rckstrt_idx < first_rd_idx

    rckstrt_clk = history[rckstrt_idx].clk
    first_rd_clk = history[first_rd_idx].clk
    assert first_rd_clk - rckstrt_clk >= dut.timing("nRCKSTRT2RD")


def test_gddr7_start_with_rckstrt_emits_one_rckstrt_for_burst_of_reads():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    for col in range(0, 8):
        dut.send_request("Read", _addr(dut, bank=0, row=0, column=col))
    history = dut.run_until_idle(max_ticks=512)

    assert [item.command for item in history].count("RCKSTRT") == 1


def test_gddr7_start_with_rckstrt_stops_after_idle_then_restarts_for_next_read():
    idle = 8
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=idle)
    dut.send_request("Read", _addr(dut, bank=0, row=0))
    first_pass = dut.run_until_idle(
        max_ticks=idle + dut.timing("nRD2RCKSTOP") + dut.timing("nRCKSTOP_LAT") + 64,
    )

    first_cmds = [item.command for item in first_pass]
    assert first_cmds.count("RCKSTRT") == 1
    assert first_cmds.count("RCKSTOP") == 1

    dut.send_request("Read", _addr(dut, bank=1, row=0))
    second_pass = dut.run_until_idle(max_ticks=256)
    second_cmds = [item.command for item in second_pass]
    assert second_cmds.count("RCKSTRT") == 1

    last_rckstop_clk = max(item.clk for item in first_pass if item.command == "RCKSTOP")
    next_rckstrt_clk = next(item.clk for item in second_pass if item.command == "RCKSTRT")
    assert next_rckstrt_clk - last_rckstop_clk >= dut.timing("nRCKSP2ST")

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

def _drain(dut, n):
    for _ in range(n):
        dut.tick()


def test_gddr7_two_column_commands_serialize_on_column_bus():
    # Only one column command may issue per cycle (single column-bus slot), and
    # successive column commands respect nCCD (2-cycle command occupancy).
    dut = _make_gddr7()
    a0 = _addr(dut, bank=0, row=0)
    a1 = _addr(dut, bank=1, row=0)

    dut.priority_send("ACT", a0)
    dut.priority_send("ACT", a1)
    _drain(dut, dut.timing("nRRD") + dut.timing("nRCDRD") + 8)  # open both rows

    dut.priority_send("RD", a0)
    dut.priority_send("RD", a1)
    rds = []
    for _ in range(dut.timing("nCCD") + 8):
        issued = dut.tick()
        assert sum(1 for i in issued if i.command in ("RD", "RDA")) <= 1
        rds += [i for i in issued if i.command == "RD"]
        if len(rds) == 2:
            break

    assert len(rds) == 2
    assert rds[1].clk - rds[0].clk >= dut.timing("nCCD")


def test_gddr7_two_row_commands_serialize_on_row_bus():
    # Only one row command may issue per cycle (single row-bus slot); two ACTs
    # to different banks are spaced by nRRD.
    dut = _make_gddr7()
    dut.priority_send("ACT", _addr(dut, bank=0, row=0))
    dut.priority_send("ACT", _addr(dut, bank=1, row=0))

    acts = []
    for _ in range(dut.timing("nRRD") + 8):
        issued = dut.tick()
        assert sum(1 for i in issued if i.command == "ACT") <= 1
        acts += [i for i in issued if i.command == "ACT"]
        if len(acts) == 2:
            break

    assert len(acts) == 2
    assert acts[1].clk - acts[0].clk >= dut.timing("nRRD")


def test_gddr7_rckstrt_and_read_do_not_co_issue_on_column_bus():
    # RCKSTRT/RCKSTOP are column commands, so they share the single column-bus
    # slot with RD/RDA and can never co-issue with a read in the same cycle.
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    dut.send_request("Read", _addr(dut, bank=0, row=0))
    history = dut.run_until_idle(max_ticks=128)

    by_clk = {}
    for item in history:
        by_clk.setdefault(item.clk, []).append(item.command)
    for clk, cmds in by_clk.items():
        cols = [c for c in cmds if c in ("RD", "RDA", "RCKSTRT", "RCKSTOP")]
        assert len(cols) <= 1, f"two column commands issued at clk {clk}: {cols}"


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
    # dut = _make_gddr7()
    # Manual RCKSTRT/RCKSTOP injection is only legal in start/stop modes
    # (illegal in 'disabled' and 'always_on' per JESD239D §6.9).
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    rck = _all_bank_addr(dut)

    dut.priority_send("RCKSTRT", rck)
    dut.priority_send("RCKSTOP", rck)
    history = dut.run_until_idle(max_ticks=64)

    assert [item.command for item in history] == ["RCKSTRT", "RCKSTOP"]
    assert history[1].clk - history[0].clk == dut.timing("nRCKST2SP")


# ── PAM3 / NRZ encoding ────────────────────────────────────────────────────


def test_gddr7_default_encoding_is_pam3():
    dut = _make_gddr7()
    assert dut.timing("nBL") == 2
    assert dut.timing("nCCD") == 2


def test_gddr7_explicit_pam3_encoding_yields_nbl_2():
    dut = _make_gddr7(_dram(encoding="PAM3"))
    assert dut.timing("nBL") == 2
    assert dut.timing("nCCD") == 2


def test_gddr7_nrz_encoding_yields_nbl_4_and_nccd_4():
    dut = _make_gddr7(_dram(encoding="NRZ"))
    assert dut.timing("nBL") == 4
    assert dut.timing("nCCD") == 4
    # tCCDSB is the same in both modes (same-bank constraint).
    assert dut.timing("nCCDSB") == 4


def test_gddr7_unknown_encoding_is_rejected():
    # Validation runs at resolve time (to_config triggers resolve).
    with pytest.raises(ValueError, match="encoding must be one of"):
        _dram(encoding="PAM5").to_config()
