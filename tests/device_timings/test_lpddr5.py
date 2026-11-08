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


@pytest.mark.parametrize("timing_preset", ["LPDDR5_6400", "LPDDR5X_8533"])
def test_lpddr5_ras_to_cas_delay_differs_for_read_and_write(timing_preset):
    # JESD209-5C Table 396 note 1: on LPDDR5X tRCD from ACT-2 to WRITE is max(8ns, 2nCK); to READ it stays
    # max(18ns, 2nCK). LPDDR5 has one tRCD (Table 381).
    dram = ramulator.dram.LPDDR5(org_preset="LPDDR5_16Gb_x16", timing_preset=timing_preset)
    dut = device_timings.DeviceUnderTest(dram)
    row = _addr(dut, bankgroup=0, bank=0, row=0)
    dut.issue("ACT1", row, clk=0)
    dut.issue("ACT2", row, clk=1)
    dut.assert_earliest_ready_at("RD", row, 1 + dut.timings["nRCD"])
    dut.assert_earliest_ready_at("WR", row, 1 + dut.timings["nRCDW"])
    if timing_preset == "LPDDR5X_8533":
        assert dut.timings["nRCDW"] < dut.timings["nRCD"]
