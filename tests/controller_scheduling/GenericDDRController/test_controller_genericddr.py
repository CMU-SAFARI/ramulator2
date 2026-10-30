import pytest

import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling


def make_dut(*, row_policy=None):
    dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
    return cs.ControllerUnderTest.make_generic_ddr(
        dram,
        row_policy=row_policy or ramulator.row_policy.Open(),
    )


def make_gddr6_dut(*, scheduler=None):
    dram = ramulator.dram.GDDR6(
        org_preset="GDDR6_8Gb_x16",
        timing_preset="GDDR6_14000_1250mV_double",
    )
    return cs.ControllerUnderTest.make_generic_ddr(
        dram,
        scheduler=scheduler or ramulator.scheduler.FRFCFS(),
        read_buffer_size=128,
        write_buffer_size=128,
    )


def test_row_miss_emits_act_then_rd():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)

    dut.send_request("Read", a)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD"], history=history)
    dut.assert_gap(0, 1, dut.timings["nRCD"], history=history)


def test_row_hit_follow_up_emits_only_rd():
    dut = make_dut()
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)

    dut.send_request("Read", a)
    dut.send_request("Read", a)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD", "RD"], history=history)
    dut.assert_gap(0, 1, dut.timings["nRCD"], history=history)
    dut.assert_gap(1, 2, dut.timings["nCCDL"], history=history)


def test_row_conflict_emits_prepb_act_rd():
    dut = make_dut()
    a0 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    a1 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=1, Column=0)

    dut.send_request("Read", a0)
    dut.send_request("Read", a1)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD", "PREpb", "ACT", "RD"], history=history)
    dut.assert_gap(0, 1, dut.timings["nRCD"], history=history)
    dut.assert_gap(0, 2, dut.timings["nRAS"], history=history)
    dut.assert_gap(2, 3, dut.timings["nRP"], history=history)
    dut.assert_gap(3, 4, dut.timings["nRCD"], history=history)


def test_frfcfs_prefers_ready_request_over_older_blocked_request():
    dut = make_dut()
    row0 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    row1 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=1, Column=0)

    dut.send_request("Read", row0)
    dut.send_request("Read", row1)
    dut.send_request("Read", row0)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD", "RD", "PREpb", "ACT", "RD"], history=history)


def test_frfcfs_rowhit_keeps_same_bank_row_hits_before_conflict_close():
    dut = make_gddr6_dut(scheduler=ramulator.scheduler.FRFCFSRowHit())
    row_level = dut.level_names.index("Row")
    prefetch = type(dut.dram).internal_prefetch_size
    num_cls = dut.org["column"] // prefetch

    def addr(row, column):
        return dut.addr_vec(Channel=0, BankGroup=0, Bank=0, Row=row, Column=column)

    for cls in range(num_cls):
        dut.send_request("Read", addr(row=10, column=cls * prefetch))
    dut.send_request("Read", addr(row=11, column=0))

    history = dut.run_until_idle(max_ticks=4096)
    first_pre = next(i for i, item in enumerate(history) if item.command == "PREpb")

    row10_rds_before_pre = [
        item
        for item in history[:first_pre]
        if item.command == "RD" and item.addr_vec[row_level] == 10
    ]
    row10_acts = [
        item
        for item in history
        if item.command == "ACT" and item.addr_vec[row_level] == 10
    ]

    assert len(row10_rds_before_pre) == num_cls
    assert len(row10_acts) == 1
    assert [item.command for item in history].count("PREpb") == 1


def test_closedcap_upgrades_second_access_to_rda():
    dut = make_dut(row_policy=ramulator.row_policy.ClosedCAP(cap=1))
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)

    dut.send_request("Read", a)
    dut.send_request("Read", a)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD", "RDA"], history=history)


def test_priority_send_tracks_maintenance_stats():
    dut = make_dut()
    refresh = dut.addr_vec(Rank=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=0)

    dut.priority_send("REFab", refresh)
    history = dut.run_until_idle(max_ticks=128)
    stats = dut.stats()

    dut.assert_commands(["REFab"], history=history)
    assert stats["num_maintenance_reqs"] == 1
    assert stats["num_maintenance_reqs_served"] == 1


def test_pending_refpb_holds_back_only_the_bank_it_refreshes():
    dut = make_gddr6_dut()

    def refreshed(column):
        return dut.addr_vec(BankGroup=0, Bank=0, Row=0, Column=column)

    def other(column):
        return dut.addr_vec(BankGroup=1, Bank=1, Row=0, Column=column)

    dut.send_request("Read", refreshed(0))
    dut.send_request("Read", other(0))
    dut.run_until_idle(max_ticks=128)

    # The refreshed bank is open, so the REFpb waits tRP after its PREpb. Row hits to the other
    # bank go on meanwhile; the row hit to the refreshed bank waits for the refresh.
    dut.priority_send("REFpb", dut.addr_vec(BankGroup=0, Bank=0, Row=dut.ALL, Column=dut.ALL))
    dut.send_request("Read", refreshed(16))
    for i in range(1, 9):
        dut.send_request("Read", other(16 * i))
    history = dut.run_until_idle(max_ticks=512)

    refpb = next(item for item in history if item.command == "REFpb")
    reads = [item for item in history if item.command == "RD"]
    other_reads = [item.clk for item in reads if item.addr_vec[1:3] == [1, 1]]
    refreshed_read = next(item for item in reads if item.addr_vec[1:3] == [0, 0])
    assert len(other_reads) == 8
    assert max(other_reads) < refpb.clk
    assert refreshed_read.clk > refpb.clk


def test_controller_addr_vec_rejects_none():
    dut = make_dut()

    with pytest.raises(TypeError, match="expects an int"):
        dut.addr_vec(Rank=None)
