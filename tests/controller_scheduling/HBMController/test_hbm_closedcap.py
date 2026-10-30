import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _addr(dut, *, bankgroup, column=0):
    return dut.addr_vec(Channel=0, BankGroup=bankgroup, Bank=0, Row=0, Column=column)


def _bank(item):
    return item.addr_vec[1:3]


def test_closedcap_keeps_a_bank_open_for_an_activated_request():
    # A write to bank group 0 is activated, then held back by tRTW behind reads to bank group 1.
    # Row hits to its bank meanwhile reach the cap. Upgrading one of them to RDA would close the
    # bank under the write, which the HBM controller never re-activates, and the PREab in front
    # of every later refresh would wait for it forever.
    dut = cs.ControllerUnderTest.make_hbm12(
        ramulator.dram.HBM1(org_preset="HBM1_2Gb", timing_preset="HBM1_2Gbps"),
        refresh_manager=ramulator.refresh_manager.AllBank(),
        row_policy=ramulator.row_policy.ClosedCAP(cap=4),
    )
    dut.send_request("Read", _addr(dut, bankgroup=1))
    dut.run_until_idle(max_ticks=64)

    dut.send_request("Write", _addr(dut, bankgroup=0))
    while not any(item.command == "ACT" and _bank(item) == [0, 0] for item in dut.history):
        dut.tick()
    for column in (1, 2):
        dut.send_request("Read", _addr(dut, bankgroup=1, column=column))
    for column in range(1, 6):
        dut.send_request("Read", _addr(dut, bankgroup=0, column=column))
    dut.run_until_idle(max_ticks=256)

    write = next(item for item in dut.history if item.command in ("WR", "WRA"))
    closing = ("RDA", "WRA", "PREpb")
    closed_early = [
        item for item in dut.history
        if item.command in closing and _bank(item) == [0, 0] and item.clk < write.clk
    ]
    assert closed_early == []

    for _ in range(2 * dut.timing("nREFI")):
        dut.tick()
    assert sum(item.command == "REFab" for item in dut.history) >= 2
