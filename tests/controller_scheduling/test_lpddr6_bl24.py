import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def make_lpddr6_dut(*, refresh_manager=None, row_policy=None, dram_kwargs=None, **controller_kwargs):
    dram = ramulator.dram.LPDDR6(
        org_preset="LPDDR6_16Gb_x16_payload_BL24",
        timing_preset="LPDDR6_10667_BL24",
        rank=1,
        **(dram_kwargs or {}),
    )
    controller = ramulator.controller.LPDDR6(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=refresh_manager or ramulator.refresh_manager.NoRefresh(),
        row_policy=row_policy or ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        **controller_kwargs,
    )
    return cs.ControllerUnderTest(controller)


def lpddr6_addr(dut, *, bankgroup=0, bank=0, row=0, column=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank, Row=row, Column=column)


def open_row(dut, addr):
    dut.send_request("Read", addr)
    dut.run_until_idle(max_ticks=2048)


def test_lpddr6_closed_row_read_uses_short_read_command():
    dut = make_lpddr6_dut()
    addr = lpddr6_addr(dut)

    dut.send_request("Read", addr)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["ACT1", "ACT2", "CAS_RD", "RD_S"], history=history)


def test_lpddr6_closed_row_write_uses_short_write_command():
    dut = make_lpddr6_dut()
    addr = lpddr6_addr(dut)

    dut.send_request("Write", addr)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["ACT1", "ACT2", "CAS_WR", "WR_S"], history=history)


def test_lpddr6_payload_transaction_size_is_32_bytes():
    dut = make_lpddr6_dut()

    assert type(dut.dram).internal_prefetch_size * dut.org["channel_width"] // 8 == 32
    assert dut.timing("nBL_min") == 6
    assert dut.timing("nBL_max") == 12
    assert dut.timing("nRTW_S") == 41
    assert dut.timing("nRTW_L") == 47
    assert dut.timing("nRFC") == 1014


def test_lpddr6_different_bg_row_hit_reads_use_nccds():
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    first = lpddr6_addr(dut, bankgroup=0, bank=0, row=0)
    second = lpddr6_addr(dut, bankgroup=1, bank=0, row=0)
    open_row(dut, first)
    open_row(dut, second)

    dut.send_request("Read", first)
    dut.send_request("Read", second)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["RD_S", "RD_S"], history=history)
    dut.assert_gap(0, 1, dut.timing("nCCDS"), history=history)
    assert dut.timing("nCCDS") == 6


def test_lpddr6_same_bg_row_hit_reads_use_nccdl():
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    first = lpddr6_addr(dut, bankgroup=0, bank=0, row=0)
    second = lpddr6_addr(dut, bankgroup=0, bank=1, row=0)
    open_row(dut, first)
    open_row(dut, second)

    dut.send_request("Read", first)
    dut.send_request("Read", second)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["RD_S", "RD_S"], history=history)
    dut.assert_gap(0, 1, dut.timing("nCCDL"), history=history)
    assert dut.timing("nCCDL") == 10


def test_lpddr6_write_to_precharge_uses_nbl_max():
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    addr = lpddr6_addr(dut)
    open_row(dut, addr)

    dut.send_request("Write", addr)
    write_history = dut.run_until_idle(max_ticks=2048)
    dut.priority_send("PREpb", addr)
    precharge_history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["WR_S"], history=write_history)
    dut.assert_commands(["PREpb"], history=precharge_history)
    assert precharge_history[0].clk - write_history[0].clk == (
        dut.timing("nWL") + dut.timing("nBL_max") + dut.timing("nWTP")
    )


def test_lpddr6_same_bg_read_to_write_uses_nrtw_l():
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    addr = lpddr6_addr(dut)
    open_row(dut, addr)

    dut.send_request("Read", addr)
    dut.send_request("Write", addr)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["RD_S", "WR_S"], history=history)
    dut.assert_gap(0, 1, dut.timing("nRTW_L"), history=history)
    assert dut.timing("nRTW_L") == 47


def test_lpddr6_all_bank_refresh_uses_rank_scope():
    dut = make_lpddr6_dut(
        refresh_manager=ramulator.refresh_manager.AllBank(),
        dram_kwargs={"nREFI": 4},
    )

    refs = []
    for _ in range(16):
        refs.extend(item for item in dut.tick() if item.command == "REFab")
        if refs:
            break

    assert len(refs) == 1
    ref = refs[0]
    assert ref.addr_vec[dut.level_names.index("Rank")] == 0
    assert ref.addr_vec[dut.level_names.index("BankGroup")] == dut.ALL
    assert ref.addr_vec[dut.level_names.index("Bank")] == dut.ALL
