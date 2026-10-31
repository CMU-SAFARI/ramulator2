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


def _nck(ns, tCK_ps):
    # JESD79-5C §13.2: nCK = truncate(t * 0.997 / tCK + 1), in integer picoseconds.
    return ((ns * 1000 * 997) // tCK_ps + 1000) // 1000


def _mr6_setting(nck, settings):
    # The smallest MR6 setting that covers the minimum.
    return next(setting for setting in settings if setting >= nck)


def test_prac_resolves_the_jedec_prac_core_timings():
    # JESD79-5C Tables 343, 344, 346, 347 and the MR6 PRAC tRTP and tWR settings.
    timings = _resolve(prac="on")
    tCK_ps = timings["tCK_ps"]

    assert timings["nRAS"] == _nck(16, tCK_ps)
    assert timings["nRP"] == _nck(36, tCK_ps)
    assert timings["nRC"] == max(_nck(52, tCK_ps), timings["nRAS"] + timings["nRP"])
    assert timings["nRTP"] == _mr6_setting(_nck(5, tCK_ps), range(8, 23))
    assert timings["nWR"] == _mr6_setting(_nck(10, tCK_ps), range(16, 45, 2))
    assert timings["nRPab"] == _nck(36 + 4, tCK_ps)  # tRP + 4 ns for a PREab closing 9-32 rows
    assert timings["nRFM"] == _nck(5 * 70, tCK_ps)   # 5 * tRRFab
    assert timings["nRFMsb"] == _nck(5 * 60, tCK_ps)  # 5 * tRRFsb
    assert timings["nRFC"] == _resolve()["nRFC"]  # §16.4.1: tRFC is unchanged


def test_prac_off_keeps_the_speed_bin_timings():
    timings = _resolve()

    assert timings["nRP"] == ramulator.dram.DDR5.timing_presets["DDR5_6400AN"]["nRP"]
    assert timings["nRPab"] == timings["nRP"]
    assert timings["nRFM"] == timings["nRFC"]


def test_prac_on_an_8gb_device_warns_but_resolves():
    # JESD79-5C §16.1 defines PRAC for 16Gb and larger; going below is allowed with a warning.
    with pytest.warns(JEDECWarning, match="16Gb"):
        timings = _resolve(org_preset="DDR5_8Gb_x8", prac="on")

    assert timings["nRP"] == _nck(36, timings["tCK_ps"])


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
