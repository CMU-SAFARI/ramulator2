import pytest

import ramulator
import ramulator.controller_scheduling as cs


pytestmark = pytest.mark.controller_scheduling


def make_ddr4():
    return ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)


def make_dut():
    return cs.ControllerUnderTest.make_generic_ddr(
        make_ddr4(),
        row_policy=ramulator.row_policy.Open(),
    )


def test_controller_under_test_example_request_flow():
    # Build a deterministic controller stack around one DDR4 device.
    dram = make_ddr4()
    dut = cs.ControllerUnderTest.make_generic_ddr(dram)

    row0 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    row1 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=1, Column=0)

    # Two reads to different rows in the same bank create a row conflict.
    dut.send_request("Read", row0)
    dut.send_request("Read", row1)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD", "PREpb", "ACT", "RD"], history=history)
    dut.assert_gap(0, 1, dut.timings["nRCD"], history=history)
    dut.assert_gap(2, 3, dut.timings["nRP"], history=history)
    dut.assert_gap(3, 4, dut.timings["nRCD"], history=history)


def test_controller_under_test_example_priority_flow():
    # Use a fresh DUT so the maintenance example is easy to read in isolation.
    dut = make_dut()
    refresh = dut.addr_vec(Rank=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=0)

    dut.priority_send("REFab", refresh)
    history = dut.run_until_idle(max_ticks=128)
    stats = dut.stats()

    dut.assert_commands(["REFab"], history=history)
    assert stats["num_maintenance_reqs"] == 1
    assert stats["num_maintenance_reqs_served"] == 1
