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


def test_controller_addr_vec_rejects_none():
    dut = make_dut()

    with pytest.raises(TypeError, match="expects an int"):
        dut.addr_vec(Rank=None)
