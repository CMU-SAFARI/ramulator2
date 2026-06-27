import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def make_lpddr6_dut(*, refresh_manager=None, row_policy=None, dram_kwargs=None, **controller_kwargs):
    dram = ramulator.dram.LPDDR6(
        org_preset="LPDDR6_16Gb_x12",
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

    dut.assert_commands(["ACT1", "ACT2", "CAS", "RD_S"], history=history)


def test_lpddr6_closed_row_write_uses_short_write_command():
    dut = make_lpddr6_dut()
    addr = lpddr6_addr(dut)

    dut.send_request("Write", addr)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["ACT1", "ACT2", "CAS", "WR_S"], history=history)


def test_lpddr6_payload_transaction_size_is_32_bytes():
    dut = make_lpddr6_dut()

    assert type(dut.dram).data_payload_bytes == 32
    assert dut.org["channel_width"] == 12
    assert dut.timing("nBL_min") == 6
    assert dut.timing("nBL_max") == 12
    assert dut.timing("nRTW_S") == 41
    assert dut.timing("nRTW_L") == 47
    assert dut.timing("nRFC") == 1014


def test_lpddr6_act1_must_be_followed_by_act2_before_next_act1():
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    first = lpddr6_addr(dut, bankgroup=0, bank=0, row=0)
    second = lpddr6_addr(dut, bankgroup=1, bank=0, row=0)

    dut.send_request("Read", first)
    dut.send_request("Read", second)
    history = dut.run_until_idle(max_ticks=2048)

    assert [item.command for item in history[:4]] == ["ACT1", "ACT2", "ACT1", "ACT2"]


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


def test_lpddr6_same_bg_write_to_read_uses_array_cycle_time():
    # Regression guard: same-BG WRITE->READ turnaround must use BL/n_max (the
    # column array cycle time), not BL/n_min. JESD209-6 Table 383:
    # WL + BL/n_max + RU(tWTR_L/tCK). At 10667 that is 26 + 12 + 32 = 70 nCK.
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    addr = lpddr6_addr(dut)
    open_row(dut, addr)

    dut.send_request("Write", addr)
    write_history = dut.run_until_idle(max_ticks=2048)
    dut.send_request("Read", addr)
    read_history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["WR_S"], history=write_history)
    dut.assert_commands(["RD_S"], history=read_history)
    expected = dut.timing("nWL") + dut.timing("nBL_max") + dut.timing("nWTRL")
    assert read_history[0].clk - write_history[0].clk == expected
    assert expected == 70


def test_lpddr6_long_burst_quantities_match_table_381():
    dut = make_lpddr6_dut()

    # BL24 (BL/n_min, BL/n_max) and BL48 (_L) per JESD209-6 Table 381 at >6400 Mbps.
    assert dut.timing("nBL_min") == 6
    assert dut.timing("nBL_max") == 12
    assert dut.timing("nBL_min_L") == 18
    assert dut.timing("nBL_max_L") == 24
    # Same-BG tCCD after BL48 (JESD209-6 Table 382) and BL48 read-to-precharge.
    assert dut.timing("nCCDL_L") == 22
    assert dut.timing("nRTP_L") == 26
    # Read-to-write turnaround, per preceding burst length (JESD209-6 Tables 389-390).
    assert dut.timing("nRTW_S_L") == 53
    assert dut.timing("nRTW_L_L") == 59


def test_lpddr6_long_read_to_precharge_uses_nrtp_l():
    # RD_L is not issued on the normal request path; inject it directly to
    # exercise the BL48 read-to-precharge constraint (nRTP_L).
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    addr = lpddr6_addr(dut)
    open_row(dut, addr)

    dut.priority_send("RD_L", addr)
    read_history = dut.run_until_idle(max_ticks=2048)
    dut.priority_send("PREpb", addr)
    pre_history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["RD_L"], history=read_history)
    dut.assert_commands(["PREpb"], history=pre_history)
    assert pre_history[0].clk - read_history[0].clk == dut.timing("nRTP_L")
    assert dut.timing("nRTP_L") == 26


def test_lpddr6_same_bg_long_read_to_read_uses_nccdl_l():
    # Two injected same-BG BL48 reads must be spaced by nCCDL_L (JESD209-6 Table 382).
    dut = make_lpddr6_dut(wck_sync_mode="always_on")
    first = lpddr6_addr(dut, bankgroup=0, bank=0, row=0)
    second = lpddr6_addr(dut, bankgroup=0, bank=1, row=0)
    open_row(dut, first)
    open_row(dut, second)

    dut.priority_send("RD_L", first)
    dut.priority_send("RD_L", second)
    history = dut.run_until_idle(max_ticks=2048)

    dut.assert_commands(["RD_L", "RD_L"], history=history)
    dut.assert_gap(0, 1, dut.timing("nCCDL_L"), history=history)
    assert dut.timing("nCCDL_L") == 22


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
