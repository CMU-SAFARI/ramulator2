import pytest

import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling


def _dram(**overrides):
    return ramulator.dram.GDDR7(
        org_preset="GDDR7_16Gb_x8",
        timing_preset="GDDR7_28000_PAM3",
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


def test_gddr7_full_device_is_composed_from_four_single_channel_controllers():
    controllers = [
        ramulator.controller.GDDR7(
            dram=_dram(),
            scheduler=ramulator.scheduler.FRFCFS(),
            refresh_manager=ramulator.refresh_manager.NoRefresh(),
            row_policy=ramulator.row_policy.Open(),
            addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        )
        for _ in range(4)
    ]
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=controllers,
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
    )

    cfg = mem.to_config()

    assert len(cfg["controllers"]) == 4
    for ctrl in cfg["controllers"]:
        assert ctrl["dram"]["org"]["count"][0] == 1
        assert ctrl["dram"]["org"]["count"][1] == 16
        assert ctrl["dram"]["channel_width"] == 8


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
    stats = dut.stats()
    assert stats["num_rckstrt_issued"] == 0
    assert stats["num_rckstop_issued"] == 0


def test_gddr7_start_with_read_never_emits_rckstrt_and_stops_after_idle():
    idle = 4
    dut = _make_gddr7(rck_mode="start_with_read", rck_idle_threshold=idle)
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    history = dut.run_until_idle(max_ticks=128)
    commands = [item.command for item in history]

    assert "RCKSTRT" not in commands
    assert commands.count("RCKSTOP") == 1

    last_rd_clk = max(item.clk for item in history if item.command in ("RD", "RDA"))
    rckstop_clk = next(item.clk for item in history if item.command == "RCKSTOP")
    assert rckstop_clk - last_rd_clk >= dut.timing("nRD2RCKSTOP")
    stats = dut.stats()
    assert stats["num_rckstrt_issued"] == 0
    assert stats["num_rckstop_issued"] == 1


def test_gddr7_start_with_read_rejects_manual_rckstrt():
    dut = _make_gddr7(rck_mode="start_with_read")

    with pytest.raises(RuntimeError, match="RCKSTRT"):
        dut.priority_send("RCKSTRT", _all_bank_addr(dut))


def test_gddr7_start_with_rckstrt_emits_rckstrt_before_first_read():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    history = dut.run_until_idle(max_ticks=128)
    commands = [item.command for item in history]
    rckstrt_idx = commands.index("RCKSTRT")
    first_rd_idx = commands.index("RD")

    assert rckstrt_idx < first_rd_idx
    assert history[first_rd_idx].clk - history[rckstrt_idx].clk >= dut.timing("nRCKSTRT2RD")


def test_gddr7_start_with_rckstrt_emits_one_rckstrt_for_burst_of_reads():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    for col in range(8):
        dut.send_request("Read", _addr(dut, bank=0, row=0, column=col))

    history = dut.run_until_idle(max_ticks=512)

    assert [item.command for item in history].count("RCKSTRT") == 1


def test_gddr7_start_with_rckstrt_stops_after_idle_then_restarts_for_next_read():
    idle = 4
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=idle)
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    first_pass = dut.run_until_idle(max_ticks=128)
    first_commands = [item.command for item in first_pass]
    assert first_commands.count("RCKSTRT") == 1
    assert first_commands.count("RCKSTOP") == 1

    dut.send_request("Read", _addr(dut, bank=1, row=0))
    second_pass = dut.run_until_idle(max_ticks=256)
    second_commands = [item.command for item in second_pass]
    assert second_commands.count("RCKSTRT") == 1

    last_rckstop_clk = max(item.clk for item in first_pass if item.command == "RCKSTOP")
    next_rckstrt_clk = next(item.clk for item in second_pass if item.command == "RCKSTRT")
    assert next_rckstrt_clk - last_rckstop_clk >= dut.timing("nRCKSP2ST")
    stats = dut.stats()
    assert stats["num_rckstrt_issued"] == 2
    assert stats["num_rckstop_issued"] == 2


def test_gddr7_rck_commands_serialize_on_column_bus():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    history = dut.run_until_idle(max_ticks=128)

    for clk in {item.clk for item in history}:
        commands = [item.command for item in history if item.clk == clk]
        column_commands = [cmd for cmd in commands if cmd in ("RD", "RDA", "WR", "WRA", "RCKSTRT", "RCKSTOP")]
        assert len(column_commands) <= 1


def test_gddr7_internal_rckstrt_ignores_full_priority_buffer():
    dut = _make_gddr7(
        rck_mode="start_with_rckstrt",
        rck_idle_threshold=1_000_000,
        priority_buffer_size=1,
    )
    dut.priority_send("PREab", _all_bank_addr(dut))
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    history = dut.run_until_idle(max_ticks=256)
    commands = [item.command for item in history]

    assert "PREab" in commands
    assert commands.index("RCKSTRT") < commands.index("RD")


def test_gddr7_internal_rckstrt_does_not_depend_on_priority_buffer_capacity():
    dut = _make_gddr7(
        rck_mode="start_with_rckstrt",
        rck_idle_threshold=1_000_000,
        priority_buffer_size=0,
    )
    dut.send_request("Read", _addr(dut, bank=0, row=0))

    history = dut.run_until_idle(max_ticks=128)
    commands = [item.command for item in history]

    assert commands.index("RCKSTRT") < commands.index("RD")


def test_gddr7_frfcfs_rowhit_protects_column_hits_from_row_slot_prepb():
    dut = _make_gddr7(scheduler=ramulator.scheduler.FRFCFSRowHit(), read_buffer_size=128)
    row_level = _level_index(dut, "Row")
    open_row = _addr(dut, bank=0, row=0, column=0)
    conflict = _addr(dut, bank=0, row=1, column=0)

    dut.send_request("Read", open_row)
    dut.run_until_idle(max_ticks=128)

    for column in range(4):
        dut.send_request("Read", _addr(dut, bank=0, row=0, column=column))
    dut.send_request("Read", conflict)

    history = dut.run_until_idle(max_ticks=512)
    first_pre = next(i for i, item in enumerate(history) if item.command == "PREpb")
    row0_rds_before_pre = [
        item
        for item in history[:first_pre]
        if item.command == "RD" and item.addr_vec[row_level] == 0
    ]

    assert len(row0_rds_before_pre) == 4


def test_gddr7_manual_rfm_commands_use_device_command_plumbing():
    dut = _make_gddr7()

    dut.priority_send("RFMpb", _addr(dut, bank=0))
    dut.priority_send("RFMab", _all_bank_addr(dut))
    history = dut.run_until_idle(max_ticks=16)

    assert [item.command for item in history] == ["RFMpb", "RFMab"]


def test_gddr7_always_on_rejects_manual_rck_commands():
    dut = _make_gddr7()
    rck = _all_bank_addr(dut)

    with pytest.raises(RuntimeError, match="RCKSTRT/RCKSTOP"):
        dut.priority_send("RCKSTRT", rck)
    with pytest.raises(RuntimeError, match="RCKSTRT/RCKSTOP"):
        dut.priority_send("RCKSTOP", rck)


def test_gddr7_start_with_rckstrt_manual_rck_commands_follow_timing_model():
    dut = _make_gddr7(rck_mode="start_with_rckstrt", rck_idle_threshold=1_000_000)
    rck = _all_bank_addr(dut)

    dut.priority_send("RCKSTRT", rck)
    dut.priority_send("RCKSTOP", rck)
    history = dut.run_until_idle(max_ticks=64)

    assert [item.command for item in history] == ["RCKSTRT", "RCKSTOP"]
    assert history[1].clk - history[0].clk == dut.timing("nRCKST2SP")
