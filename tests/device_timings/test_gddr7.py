import pytest

import ramulator
from ramulator.dram.gddr7 import GDDR7
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


COMMAND_CYCLES = {
    "ACT": 2,
    "RD": 2,
    "RDA": 2,
    "WR": 2,
    "WRA": 2,
    "RCKSTRT": 2,
    "RCKSTOP": 2,
}


def first_cycle_gap(preceding, following, nominal):
    """Match DRAMStandard.to_config() multi-cycle command adjustment."""
    return nominal + COMMAND_CYCLES.get(preceding, 1) - COMMAND_CYCLES.get(following, 1)


def make_dut(*, channel_id=0, **overrides):
    dram = ramulator.dram.GDDR7(**{
        "org_preset": "GDDR7_16Gb_x8_4ch",
        # "timing_preset": "GDDR7_TEST_28000_PAM3",
        "timing_preset": "GDDR7_TEST_28000",
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def addr(dut, *, bank=0, row=0, column=0):
    return dut.addr_vec(Channel=0, Bank=bank, Row=row, Column=column)


def all_bank_addr(dut):
    return dut.addr_vec(Channel=0, Bank=dut.ALL, Row=dut.ALL, Column=0)


def test_gddr7_resolver_uses_direct_gddr6_guesstimates_and_preserves_sources():
    # timing = {"nBL": 2, "tCK_ps": 571, "nRL": 42, "nRFCpb": 77}
    # GDDR7.resolve_secondary_timings(timing, {})

    timing = {"tCK_ps": 571, "nRL": 42, "nRFCpb": 77}
    GDDR7.resolve_secondary_timings(timing, {"encoding": "PAM3"})

    assert timing["nBL"] == 2
    assert timing["nRL"] == 42
    assert timing["nRFCpb"] == 77
    assert timing["nWL"] == 6
    assert timing["nRC"] == 90
    assert timing["nRRD"] == 11
    assert timing["nREFI"] == 3327
    assert timing["nREFIpb"] == 207
    assert timing["nRCKEN"] == 6
    assert timing["nRCKSTOP_LAT"] == 10
    assert timing["nRCK_LS"] == 2
    assert timing["nRFMpb"] == 77

    # nrz_timing = {"nBL": 4, "tCK_ps": 571}
    # GDDR7.resolve_secondary_timings(nrz_timing, {})
    nrz_timing = {"tCK_ps": 571}
    GDDR7.resolve_secondary_timings(nrz_timing, {"encoding": "NRZ"})
    assert nrz_timing["nBL"] == 4
    assert nrz_timing["nCCD"] == 4
    assert nrz_timing["nRRD"] == 17


def test_gddr7_resolver_defaults_to_pam3_when_encoding_unset():
    timing = {"tCK_ps": 571}
    GDDR7.resolve_secondary_timings(timing, {})
    assert timing["nBL"] == 2
    assert timing["nCCD"] == 2


def test_gddr7_resolver_rate_halves_in_nrz_versus_pam3():
    pam3 = {"tCK_ps": 571}
    nrz = {"tCK_ps": 571}
    GDDR7.resolve_secondary_timings(pam3, {"encoding": "PAM3"})
    GDDR7.resolve_secondary_timings(nrz, {"encoding": "NRZ"})
    # rate = internal_prefetch_size * 1e6 / (nBL * tCK_ps); nBL doubles in NRZ.
    # Allow ±1 for the round() in the formula.
    assert abs(pam3["rate"] - 2 * nrz["rate"]) <= 2


def test_gddr7_resolver_rejects_unknown_encoding():
    with pytest.raises(ValueError, match="encoding must be one of"):
        GDDR7.resolve_secondary_timings({"tCK_ps": 571}, {"encoding": "PAM7"})


def test_gddr7_resolver_rejects_preset_setting_nbl_directly():
    with pytest.raises(ValueError, match="do not set nBL"):
        GDDR7.resolve_secondary_timings(
            {"tCK_ps": 571, "nBL": 3},
            {"encoding": "PAM3"},
        )



def test_gddr7_closed_bank_read_requires_activate():
    dut = make_dut()
    a = addr(dut)

    probe = dut.probe("RD", a, clk=0)

    assert probe.preq == "ACT"
    assert probe.timing_OK is True
    assert probe.ready is False
    assert probe.row_hit is False
    assert probe.row_open is False


def test_gddr7_act_read_write_precharge_and_row_conflict_timing():
    read_dut = make_dut()
    a = addr(read_dut, row=0)
    conflict = addr(read_dut, row=1)

    read_dut.assert_earliest_ready_at("ACT", a, 0)
    read_dut.issue("ACT", a, 0)
    assert read_dut.probe("RD", conflict, clk=0).preq == "PREpb"

    t_rd = first_cycle_gap("ACT", "RD", read_dut.timings["nRCDRD"])
    read_dut.assert_earliest_ready_at("RD", a, t_rd)
    read_dut.issue("RD", a, t_rd)

    t_pre = max(
        first_cycle_gap("ACT", "PREpb", read_dut.timings["nRAS"]),
        t_rd + first_cycle_gap("RD", "PREpb", read_dut.timings["nRTPSB"]),
    )
    read_dut.assert_earliest_ready_at("PREpb", a, t_pre)
    read_dut.issue("PREpb", a, t_pre)

    t_react = t_pre + first_cycle_gap("PREpb", "ACT", read_dut.timings["nRP"])
    read_dut.assert_earliest_ready_at("ACT", conflict, t_react)

    write_dut = make_dut()
    b = addr(write_dut)
    write_dut.issue("ACT", b, 0)
    t_wr = first_cycle_gap("ACT", "WR", write_dut.timings["nRCDWR"])
    write_dut.assert_earliest_ready_at("WR", b, t_wr)
    write_dut.issue("WR", b, t_wr)

    t_pre = max(
        first_cycle_gap("ACT", "PREpb", write_dut.timings["nRAS"]),
        t_wr + first_cycle_gap("WR", "PREpb", write_dut.timings["nWL"] + write_dut.timings["nBL"] + write_dut.timings["nWR"]),
    )
    write_dut.assert_earliest_ready_at("PREpb", b, t_pre)


def test_gddr7_column_spacing_and_turnaround_timing():
    dut = make_dut()
    a0 = addr(dut, bank=0, row=0)
    a1 = addr(dut, bank=1, row=1)

    dut.issue("ACT", a0, 0)
    t_act1 = first_cycle_gap("ACT", "ACT", dut.timings["nRRD"])
    dut.assert_earliest_ready_at("ACT", a1, t_act1)
    dut.issue("ACT", a1, t_act1)

    t_rd = t_act1 + first_cycle_gap("ACT", "RD", dut.timings["nRCDRD"])
    dut.issue("RD", a0, t_rd)
    t_rd2 = t_rd + first_cycle_gap("RD", "RD", dut.timings["nCCD"])
    dut.assert_earliest_ready_at("RD", a1, t_rd2)
    dut.issue("RD", a1, t_rd2)

    t_wr = t_rd2 + first_cycle_gap("RD", "WR", dut.timings["nRTW"])
    dut.assert_earliest_ready_at("WR", a0, t_wr)
    dut.issue("WR", a0, t_wr)

    t_rd3 = t_wr + first_cycle_gap("WR", "RD", dut.timings["nWL"] + dut.timings["nBL"] + dut.timings["nWTR"])
    dut.assert_earliest_ready_at("RD", a1, t_rd3)


def test_gddr7_same_bank_column_spacing_uses_nccdsb_and_nwtrsb():
    dut = make_dut()
    a = addr(dut)

    dut.issue("ACT", a, 0)
    t_wr = first_cycle_gap("ACT", "WR", dut.timings["nRCDWR"])
    dut.issue("WR", a, t_wr)

    t_rd = t_wr + first_cycle_gap("WR", "RD", dut.timings["nWL"] + dut.timings["nBL"] + dut.timings["nWTRSB"])
    dut.assert_earliest_ready_at("RD", a, t_rd)
    dut.issue("RD", a, t_rd)

    t_next_rd = t_rd + first_cycle_gap("RD", "RD", dut.timings["nCCDSB"])
    dut.assert_earliest_ready_at("RD", a, t_next_rd)


def test_gddr7_auto_precharge_and_refresh_timing():
    auto = make_dut(nRC=40)
    a = addr(auto, bank=0, row=0)
    auto.issue("ACT", a, 0)
    t_rda = first_cycle_gap("ACT", "RDA", auto.timings["nRCDRD"])
    auto.issue("RDA", a, t_rda)
    t_react = max(
        first_cycle_gap("ACT", "ACT", auto.timings["nRC"]),
        t_rda + first_cycle_gap("RDA", "ACT", auto.timings["nRTPSB"] + auto.timings["nRP"]),
    )
    auto.assert_earliest_ready_at("ACT", a, t_react)

    per_bank = make_dut()
    b0 = addr(per_bank, bank=0)
    b1 = addr(per_bank, bank=1)
    per_bank.issue("REFpb", b0, 0)

    same_bank_act = first_cycle_gap("REFpb", "ACT", per_bank.timings["nRFCpb"])
    per_bank.assert_earliest_ready_at("ACT", b0, same_bank_act)

    different_bank_act = first_cycle_gap("REFpb", "ACT", per_bank.timings["nRREFD"])
    per_bank.assert_earliest_ready_at("ACT", b1, different_bank_act)

    all_bank = make_dut()
    refab = all_bank_addr(all_bank)
    c0 = addr(all_bank, bank=0)
    all_bank.issue("REFab", refab, 0)
    all_bank_act = first_cycle_gap("REFab", "ACT", all_bank.timings["nRFCab"])
    all_bank.assert_earliest_ready_at("ACT", c0, all_bank_act)


def test_gddr7_refresh_and_rfm_require_closed_banks():
    dut = make_dut()
    a = addr(dut)
    dut.issue("ACT", a, 0)

    assert dut.probe("REFpb", a, clk=first_cycle_gap("ACT", "REFpb", dut.timings["nRC"])).preq == "PREpb"
    assert dut.probe("RFMpb", a, clk=first_cycle_gap("ACT", "RFMpb", dut.timings["nRC"])).preq == "PREpb"
    assert dut.probe("REFab", all_bank_addr(dut), clk=first_cycle_gap("ACT", "REFab", dut.timings["nRC"])).preq == "PREab"
    assert dut.probe("RFMab", all_bank_addr(dut), clk=first_cycle_gap("ACT", "RFMab", dut.timings["nRC"])).preq == "PREab"

    closed = make_dut()
    b = addr(closed)
    closed.issue("RFMpb", b, 0)
    t_act = first_cycle_gap("RFMpb", "ACT", closed.timings["nRFMpb"])
    closed.assert_earliest_ready_at("ACT", b, t_act)


def test_gddr7_manual_rck_timing():
    dut = make_dut()
    a = addr(dut)
    rck = all_bank_addr(dut)

    dut.issue("ACT", a, 0)
    dut.issue("RCKSTRT", rck, 4)

    t_rd = first_cycle_gap("ACT", "RD", dut.timings["nRCDRD"])
    dut.assert_earliest_ready_at("RD", a, t_rd)
    dut.issue("RD", a, t_rd)

    t_stop = t_rd + first_cycle_gap("RD", "RCKSTOP", dut.timings["nRD2RCKSTOP"])
    dut.assert_earliest_ready_at("RCKSTOP", rck, t_stop)
    dut.issue("RCKSTOP", rck, t_stop)

    t_start = t_stop + first_cycle_gap("RCKSTOP", "RCKSTRT", dut.timings["nRCKSP2ST"])
    dut.assert_earliest_ready_at("RCKSTRT", rck, t_start)



def test_gddr7_rd2rckstop_tracks_encoding():
    # nRD2RCKSTOP = RL + BL/8 + DQERL + tRCKPST - RCKSTOP_LAT, and nBL == BL/8.
    # PAM3 -> nBL=2 -> 24+2+0+2-10 = 18; NRZ -> nBL=4 -> 24+4+0+2-10 = 20.
    pam3 = make_dut(encoding="PAM3")
    nrz = make_dut(encoding="NRZ")
    assert pam3.timings["nRD2RCKSTOP"] == 18
    assert nrz.timings["nRD2RCKSTOP"] == 20

    # The device enforces the larger NRZ gap after a read.
    a = addr(nrz)
    rck = all_bank_addr(nrz)
    nrz.issue("ACT", a, 0)
    t_rd = first_cycle_gap("ACT", "RD", nrz.timings["nRCDRD"])
    nrz.issue("RD", a, t_rd)
    t_stop = t_rd + first_cycle_gap("RD", "RCKSTOP", nrz.timings["nRD2RCKSTOP"])
    nrz.assert_earliest_ready_at("RCKSTOP", rck, t_stop)


@pytest.mark.parametrize(
    "override, match",
    [
        ({"nRCKEN": 3}, "nRCKEN"),
        ({"nRCKPST": 1}, "nRCKPST"),
        ({"nRCKSTRT2RD": 1}, "nRCKSTRT2RD"),
        ({"nRCK_LS": 1}, "nRCK_LS"),
    ],
)
def test_gddr7_rejects_illegal_rck_minimums(override, match):
    timing = {"tCK_ps": 571, **override}
    with pytest.raises(ValueError, match=match):
        GDDR7.resolve_secondary_timings(timing, {"encoding": "PAM3"})