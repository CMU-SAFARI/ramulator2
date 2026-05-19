import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def make_lpddr5_dut(**controller_kwargs):
    dram = ramulator.dram.LPDDR5(
        org_preset="LPDDR5_8Gb_x16",
        timing_preset="LPDDR5_6400",
        rank=1,
    )
    controller = ramulator.controller.LPDDR5(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        **controller_kwargs,
    )
    return cs.ControllerUnderTest(controller)


def lpddr5_addr(dut, *, row=0, column=0):
    return dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=row, Column=column)


def assert_no_cas_sync(history):
    commands = [item.command for item in history]
    assert "CAS_RD" not in commands
    assert "CAS_WR" not in commands


def test_lpddr5_default_first_read_emits_cas_sync():
    dut = make_lpddr5_dut()
    addr = lpddr5_addr(dut)

    dut.send_request("Read", addr)
    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["ACT1", "ACT2", "CAS_RD", "RD"], history=history)


def test_lpddr5_default_first_write_emits_cas_sync():
    dut = make_lpddr5_dut()
    addr = lpddr5_addr(dut)

    dut.send_request("Write", addr)
    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["ACT1", "ACT2", "CAS_WR", "WR"], history=history)


def test_lpddr5_always_on_first_read_skips_cas_sync():
    dut = make_lpddr5_dut(wck_sync_mode="always_on")
    addr = lpddr5_addr(dut)

    dut.send_request("Read", addr)
    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["ACT1", "ACT2", "RD"], history=history)
    assert_no_cas_sync(history)


def test_lpddr5_always_on_first_write_skips_cas_sync():
    dut = make_lpddr5_dut(wck_sync_mode="always_on")
    addr = lpddr5_addr(dut)

    dut.send_request("Write", addr)
    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["ACT1", "ACT2", "WR"], history=history)
    assert_no_cas_sync(history)


def test_lpddr5_always_on_no_resync_after_long_idle():
    dut = make_lpddr5_dut(wck_sync_mode="always_on")
    addr = lpddr5_addr(dut)

    dut.send_request("Read", addr)
    first = dut.run_until_idle(max_ticks=256)
    dut.assert_commands(["ACT1", "ACT2", "RD"], history=first)

    idle_cycles = dut.timing("nCL") + dut.timing("nBL") + dut.timing("nWCKPST") + 32
    for _ in range(idle_cycles):
        dut.tick()

    dut.send_request("Read", addr)
    second = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["RD"], history=second)
    assert_no_cas_sync(second)


def test_lpddr5_always_on_stats_no_cas_issued():
    dut = make_lpddr5_dut(wck_sync_mode="always_on")
    addr = lpddr5_addr(dut)

    dut.send_request("Read", addr)
    dut.run_until_idle(max_ticks=256)

    stats = dut.stats()
    assert stats["cas_issued"] == 0


def test_lpddr5_invalid_wck_sync_mode_rejected():
    with pytest.raises(Exception, match="invalid wck_sync_mode"):
        make_lpddr5_dut(wck_sync_mode="not_a_mode")
