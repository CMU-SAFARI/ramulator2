import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


def make_dut(**overrides):
    dram = ramulator.dram.LPDDR5(
        org_preset="LPDDR5_8Gb_x16",
        timing_preset="LPDDR5_6400",
        **overrides,
    )
    return device_timings.DeviceUnderTest(dram)


def _addr(dut, *, bankgroup, bank, row=0):
    return dut.addr_vec(
        Rank=0,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=0,
    )


def test_lpddr5_per_bank_refresh_consumes_the_four_activate_window():
    dut = make_dut(
        nFAW=10,
        nRRDS=1,
        nPBR2PBR=1,
        nPBR2ACT=1,
        nRFCpb=1,
    )
    sequence = [
        ("ACT1", _addr(dut, bankgroup=0, bank=0, row=0)),
        ("ACT2", _addr(dut, bankgroup=0, bank=0, row=0)),
        ("REFpb", _addr(dut, bankgroup=0, bank=1)),
        ("ACT1", _addr(dut, bankgroup=1, bank=0, row=1)),
        ("ACT2", _addr(dut, bankgroup=1, bank=0, row=1)),
        ("REFpb", _addr(dut, bankgroup=1, bank=1)),
    ]
    clks = []
    next_clk = 0
    for command, address in sequence:
        clk = dut.get_first_ready_clk(command, address, start=next_clk)
        dut.issue(command, address, clk=clk)
        clks.append(clk)
        next_clk = clk + 1

    # tFAW runs from the first ACT-2 (JESD209-5C Figures 97-98).
    following = _addr(dut, bankgroup=2, bank=0, row=2)
    dut.issue("ACT1", following, clk=next_clk)
    dut.assert_earliest_ready_at("ACT2", following, clks[1] + dut.timings["nFAW"])
