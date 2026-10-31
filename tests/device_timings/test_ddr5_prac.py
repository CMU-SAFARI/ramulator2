import pytest

import ramulator
import tests.device_timings.harness as device_timings
from ramulator.dram.spec import JEDECWarning

pytestmark = pytest.mark.device_timings


def _resolve(**kwargs):
    dram = ramulator.dram.DDR5(**{
        "org_preset": "DDR5_16Gb_x8",
        "timing_preset": "DDR5_6400AN",
        **kwargs,
    })
    return dram.resolve()[1]


def test_prac_resolves_the_jedec_prac_core_timings():
    # JESD79-5C Tables 343, 344, 346, 347 and the MR6 PRAC settings, at tCK = 312 ps.
    timings = _resolve(prac="on")

    assert timings["nRAS"] == 52      # 16 ns
    assert timings["nRP"] == 116      # 36 ns
    assert timings["nRC"] == 168      # max(52 ns, nRAS + nRP)
    assert timings["nRTP"] == 16      # 5 ns, MR6 PRAC setting
    assert timings["nWR"] == 32       # 10 ns, MR6 PRAC setting
    assert timings["nRPab"] == 128    # tRP + 4 ns for a PREab closing 9-32 rows
    assert timings["nRFM"] == 1119    # 5 * tRRFab = 350 ns
    assert timings["nRFMsb"] == 959   # 5 * tRRFsb = 300 ns
    assert timings["nRFC"] == _resolve()["nRFC"]  # §16.4.1: tRFC is unchanged


def test_prac_off_keeps_the_speed_bin_timings():
    timings = _resolve()

    assert timings["nRP"] == 46
    assert timings["nRPab"] == timings["nRP"]
    assert timings["nRFM"] == timings["nRFC"]


def test_prac_on_an_8gb_device_warns_but_resolves():
    # JESD79-5C §16.1 defines PRAC for 16Gb and larger; going below is allowed with a warning.
    with pytest.warns(JEDECWarning, match="16Gb"):
        timings = _resolve(org_preset="DDR5_8Gb_x8", prac="on")

    assert timings["nRP"] == 116


def test_prac_rejects_unknown_values():
    with pytest.raises(ValueError, match="prac must be one of"):
        _resolve(prac="yes")


def test_prac_preab_holds_act_for_nrpab_and_prepb_for_nrp():
    dut = device_timings.DeviceUnderTest(
        ramulator.dram.DDR5(org_preset="DDR5_16Gb_x8", timing_preset="DDR5_6400AN", prac="on")
    )
    bank = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
    other_row = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=1, Column=0)
    rank = dut.addr_vec(Rank=0, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=dut.ALL)

    # ACT takes two cycles and is timed from the second, so it can start one cycle early.
    dut.issue("ACT", bank, clk=0)
    preab = dut.get_first_ready_clk("PREab", rank, start=1)
    dut.issue("PREab", rank, clk=preab)
    dut.assert_earliest_ready_at("ACT", other_row, preab + dut.timings["nRPab"] - 1)

    dut.issue("ACT", other_row, clk=preab + dut.timings["nRPab"] - 1)
    prepb = dut.get_first_ready_clk("PREpb", other_row, start=preab + dut.timings["nRPab"])
    dut.issue("PREpb", other_row, clk=prepb)
    dut.assert_earliest_ready_at("ACT", bank, prepb + dut.timings["nRP"] - 1)
