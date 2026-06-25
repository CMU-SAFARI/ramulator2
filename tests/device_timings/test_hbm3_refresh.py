"""HBM3 refresh / refresh-management timing-constraint regression tests.

Mirror of ``test_hbm4_refresh.py`` adapted for HBM3. HBM3 shares HBM4's
dual-bus + half-cycle row-command structure (``tick_multiplier = 2``,
``ACT`` = 1.5 CK = 3 ticks, half-cycle row commands = 1 tick, column
commands = 2 ticks), so the same adjustment helper applies.

Covers the REFpb / REFab / RFMpb / RFMab edges at the PseudoChannel,
BankGroup, and Bank scopes. Pairs with the existing ``test_hbm3.py``
suite (column / ACT / WRA-PRE coverage).
"""

import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


# Per-tick widths of HBM3 commands (tick_multiplier = 2).
_ACT_TICKS = 3       # 1.5 CK
_HALF_TICKS = 1      # 0.5 CK — PREpb / PREab / REFab / REFpb / RFMab / RFMpb
_COL_TICKS = 2       # 1.0 CK — RD / WR / RDA / WRA


def _adj(nominal: int, preceding_ticks: int, following_ticks: int) -> int:
    """Adjust a nominal latency for HBM3's mixed-width row-bus commands.

    JEDEC times these constraints from the *end* of the preceding command;
    Ramulator records the *start* tick. The two views agree when both
    commands are 1 CK wide, otherwise the gap shifts by
    ``(preceding_ticks - 1) - (following_ticks - 1)``.
    """
    return nominal + (preceding_ticks - 1) - (following_ticks - 1)


def make_dut(*, channel_id=0, **overrides):
    dram = ramulator.dram.HBM3(**{
        "org_preset": "HBM3_8Gb_8hi",
        "timing_preset": "HBM3_6400Mbps",
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def _addr(dut, *, sid=0, bankgroup=0, bank=0, row=0):
    return dut.addr_vec(
        PseudoChannel=0,
        Sid=sid,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=0,
    )


# ---------------------------------------------------------------------------
# PseudoChannel-scope: REFpb / RFMpb / REFab / RFMab
# ---------------------------------------------------------------------------


def test_hbm3_refpb_to_refpb_same_pc_uses_nrrefd():
    """Per-bank refreshes serialize on the per-PC tRREFD window."""
    dut = make_dut()
    a0 = _addr(dut, bankgroup=0, bank=0)
    a1 = _addr(dut, bankgroup=1, bank=0)

    dut.issue("REFpb", a0, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a1, expected)


def test_hbm3_refpb_to_act_diff_bank_uses_nrrefd():
    """A REFpb gates the next ACT in any other bank by tRREFD (PC scope)."""
    dut = make_dut()
    a_ref = _addr(dut, bankgroup=0, bank=0)
    a_act = _addr(dut, bankgroup=2, bank=3)

    dut.issue("REFpb", a_ref, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm3_act_to_refpb_diff_bank_uses_nrrds():
    """An ACT must wait nRRDS before a REFpb on any other bank."""
    dut = make_dut()
    a_act = _addr(dut, bankgroup=0, bank=0)
    a_ref = _addr(dut, bankgroup=1, bank=2)

    dut.issue("ACT", a_act, clk=0)
    expected = _adj(dut.timings["nRRDS"], _ACT_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a_ref, expected)


def test_hbm3_rfmpb_to_act_diff_bank_uses_nrrefd():
    """RFMpb shares REFpb's tRREFD-to-ACT gating across the pseudochannel."""
    dut = make_dut()
    a_rfm = _addr(dut, bankgroup=0, bank=0)
    a_act = _addr(dut, bankgroup=3, bank=1)

    dut.issue("RFMpb", a_rfm, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm3_act_to_rfmpb_diff_bank_uses_nrrds():
    """Symmetric to ACT→REFpb: ACT must wait nRRDS before RFMpb elsewhere."""
    dut = make_dut()
    a_act = _addr(dut, bankgroup=0, bank=0)
    a_rfm = _addr(dut, bankgroup=2, bank=0)

    dut.issue("ACT", a_act, clk=0)
    expected = _adj(dut.timings["nRRDS"], _ACT_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("RFMpb", a_rfm, expected)


def test_hbm3_refab_to_act_uses_nrfc():
    """REFab freezes the entire pseudochannel for tRFC."""
    dut = make_dut()
    a_act = _addr(dut, bankgroup=3, bank=3)

    dut.issue("REFab", dut.addr_vec(PseudoChannel=0), clk=0)
    expected = _adj(dut.timings["nRFC"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm3_rfmab_to_act_uses_nrfmab():
    """RFMab is a REFab-shaped command with its own tRFMab budget."""
    dut = make_dut()
    a_act = _addr(dut, bankgroup=1, bank=2)

    dut.issue("RFMab", dut.addr_vec(PseudoChannel=0), clk=0)
    expected = _adj(dut.timings["nRFMab"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


# ---------------------------------------------------------------------------
# Bank-scope: per-bank refresh interaction with ACT / PREpb
# ---------------------------------------------------------------------------


def test_hbm3_refpb_to_act_same_bank_uses_nrfcpb():
    """Same-bank REFpb→ACT must wait tRFCpb."""
    dut = make_dut()
    a = _addr(dut, bankgroup=0, bank=0)

    dut.issue("REFpb", a, clk=0)
    expected = _adj(dut.timings["nRFCpb"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a, expected)


def test_hbm3_rfmpb_to_act_same_bank_uses_nrfmpb():
    """Same-bank RFMpb→ACT must wait tRFMpb (REFpb-shaped per-bank constraint)."""
    dut = make_dut()
    a = _addr(dut, bankgroup=2, bank=1)

    dut.issue("RFMpb", a, clk=0)
    expected = _adj(dut.timings["nRFMpb"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a, expected)


def test_hbm3_prepb_to_refpb_same_bank_uses_nrp():
    """A bank must complete tRP after PREpb before REFpb may re-target it.

    Same-bank PREpb→REFpb is gated by nRP, but the *also-applicable*
    same-bank ACT→REFpb=nRC constraint dominates whenever PREpb is issued
    at the nRAS lower bound. To exercise the nRP gate in isolation we
    delay PREpb past the point where nRC stops dominating.
    """
    dut = make_dut()
    a = _addr(dut, bankgroup=0, bank=0)

    nRC = _adj(dut.timings["nRC"], _ACT_TICKS, _HALF_TICKS)
    nRP = _adj(dut.timings["nRP"], _HALF_TICKS, _HALF_TICKS)
    # Pick a PREpb clock so that nRP + pre_clk > nRC: then nRP gates REFpb.
    pre_clk = nRC - nRP + 8  # +8 ticks of slack — still well past nRAS

    dut.issue("ACT", a, clk=0)
    dut.issue("PREpb", a, clk=pre_clk)

    dut.assert_earliest_ready_at("REFpb", a, pre_clk + nRP)
