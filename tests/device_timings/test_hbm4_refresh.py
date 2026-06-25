"""HBM4 refresh / RFM timing-constraint coverage.

Pins the timing-edge values that the HBM4 spec declares for per-bank /
all-bank refresh (REFpb / REFab) and refresh-management (RFMpb / RFMab)
commands. Each test issues a short command sequence and uses
``assert_earliest_ready_at`` to verify the gating is tight: not legal at
``cycle - 1``, legal at ``cycle``.

The constraints exercised here are spec-declared in
``python/ramulator/dram/hbm4.py``; this file is the regression net for
that table.

HBM4 has ``tick_multiplier = 2`` and mixed command cycle counts
(ACT = 3 ticks, half-cycle row commands = 1 tick, column commands =
2 ticks). JEDEC timings are measured between command **end** times, but
Ramulator records commands at their **start** tick, so the legal start
delay between two commands depends on both cycle counts:

    legal_delay = nominal + (p_cycles - 1) - (f_cycles - 1)

The helper ``adj()`` below pre-computes that offset so the tests
read like pure constraint reads.
"""

import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


# HBM4 cycle counts (in ticks; tick_multiplier = 2):
_ACT_TICKS = 3        # 1.5 CK
_HALF_TICKS = 1       # 0.5 CK — PREpb/PREab/REFab/REFpb/RFMab/RFMpb
_COL_TICKS = 2        # 1 CK   — RD/WR/RDA/WRA (default)


def _adj(nominal, preceding_ticks, following_ticks):
    """Offset the JEDEC nominal latency for the issue-start convention."""
    return nominal + (preceding_ticks - 1) - (following_ticks - 1)


def make_dut(*, channel_id=0, **overrides):
    """HBM4 32Gb 8Hi @ 8000Mbps. Override any timing via kwargs."""
    dram = ramulator.dram.HBM4(**{
        "org_preset": "HBM4_32Gb_8Hi",
        "timing_preset": "HBM4_8000Mbps",
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def _addr(dut, *, sid=0, bankgroup=0, bank=0, row=0, column=0, pseudochannel=0):
    return dut.addr_vec(
        PseudoChannel=pseudochannel,
        Sid=sid,
        BankGroup=bankgroup,
        Bank=bank,
        Row=row,
        Column=column,
    )


def _refpb_addr(dut, *, sid=0, bankgroup=0, bank=0, pseudochannel=0):
    """REFpb / RFMpb don't carry row/column — use 0 for those."""
    return _addr(dut, sid=sid, bankgroup=bankgroup, bank=bank, pseudochannel=pseudochannel)


# ─────────────────────────────────────────────────────────────────────
# Per-bank refresh (REFpb) — PseudoChannel-scope edges
# ─────────────────────────────────────────────────────────────────────

def test_hbm4_refpb_to_refpb_same_pc_diff_bank_uses_nrrefd():
    """REFpb → REFpb on different banks in the same pseudo-channel: tRREFD."""
    dut = make_dut()
    a0 = _refpb_addr(dut, sid=0, bankgroup=0, bank=0)
    a1 = _refpb_addr(dut, sid=0, bankgroup=0, bank=1)

    dut.issue("REFpb", a0, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a1, expected)


def test_hbm4_refpb_to_act_diff_bank_uses_nrrefd():
    """REFpb → ACT on a different bank: tRREFD."""
    dut = make_dut()
    a0 = _refpb_addr(dut, sid=0, bankgroup=1, bank=0)
    a1 = _addr(dut, sid=0, bankgroup=2 % dut.org["bankgroup"], bank=0, row=0)

    dut.issue("REFpb", a0, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a1, expected)


def test_hbm4_act_to_refpb_diff_bg_uses_nrrds():
    """ACT → REFpb in a different bank group: short tRRD (tRRDS)."""
    dut = make_dut()
    a_act = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a_ref = _refpb_addr(dut, sid=0, bankgroup=1 % dut.org["bankgroup"], bank=0)

    dut.issue("ACT", a_act, clk=0)
    expected = _adj(dut.timings["nRRDS"], _ACT_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a_ref, expected)


def test_hbm4_act_to_refpb_same_bg_diff_bank_uses_nrrdl():
    """ACT → REFpb in the *same* bank group: long tRRD (tRRDL).

    Pins the BankGroup-scope edge that HBM4 declares explicitly so a
    same-BG refresh is constrained tighter than the cross-BG case.
    """
    dut = make_dut()
    a_act = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)
    a_ref = _refpb_addr(dut, sid=0, bankgroup=0, bank=1)

    dut.issue("ACT", a_act, clk=0)
    expected = _adj(dut.timings["nRRDL"], _ACT_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a_ref, expected)


# Note on REFpb → ACT same BG: this edge exists at the BankGroup scope
# (nRRDL) but is dominated by the PseudoChannel-scope nRREFD on every
# HBM4 timing preset (nRREFD > nRRDL by construction in JEDEC). The
# BankGroup edge is regression-covered by the symmetric ACT → REFpb test
# above instead, where the PC-level edge uses the looser nRRDS.


# ─────────────────────────────────────────────────────────────────────
# All-bank refresh (REFab) — PseudoChannel-scope barrier
# ─────────────────────────────────────────────────────────────────────

def test_hbm4_refab_to_act_uses_nrfc():
    """REFab → ACT on any bank in the same PC: tRFC barrier."""
    dut = make_dut()
    a_ref = dut.addr_vec(
        PseudoChannel=0, Sid=dut.ALL, BankGroup=dut.ALL, Bank=dut.ALL, Row=0, Column=0
    )
    a_act = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)

    dut.issue("REFab", a_ref, clk=0)
    expected = _adj(dut.timings["nRFC"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm4_refab_to_refpb_uses_nrfc():
    """REFab → REFpb in the same PC: tRFC barrier.

    HBM4 explicitly includes REFpb in the REFab post-barrier's following
    set, so a per-bank refresh can't sneak in during the all-bank window.
    """
    dut = make_dut()
    a_refab = dut.addr_vec(
        PseudoChannel=0, Sid=dut.ALL, BankGroup=dut.ALL, Bank=dut.ALL, Row=0, Column=0
    )
    a_refpb = _refpb_addr(dut, sid=0, bankgroup=0, bank=0)

    dut.issue("REFab", a_refab, clk=0)
    expected = _adj(dut.timings["nRFC"], _HALF_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("REFpb", a_refpb, expected)


# ─────────────────────────────────────────────────────────────────────
# RFM (refresh-management) — RFMpb / RFMab edges
# ─────────────────────────────────────────────────────────────────────

def test_hbm4_rfmpb_to_rfmpb_same_pc_diff_bank_uses_nrrefd():
    """RFMpb → RFMpb on different banks in the same PC: tRREFD.

    RFM commands share tRREFD with the REFpb family. Pin this so a
    refactor that drops the RFMpb self-edge starts failing here instead
    of silently in the field.
    """
    dut = make_dut()
    a0 = _refpb_addr(dut, sid=0, bankgroup=0, bank=0)
    a1 = _refpb_addr(dut, sid=0, bankgroup=0, bank=1)

    dut.issue("RFMpb", a0, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("RFMpb", a1, expected)


def test_hbm4_rfmpb_to_act_diff_bank_uses_nrrefd():
    """RFMpb → ACT on a different bank: tRREFD."""
    dut = make_dut()
    a_rfm = _refpb_addr(dut, sid=0, bankgroup=1, bank=0)
    a_act = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)

    dut.issue("RFMpb", a_rfm, clk=0)
    expected = _adj(dut.timings["nRREFD"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm4_rfmab_to_act_uses_nrfmab():
    """RFMab → ACT on any bank in the same PC: tRFMab barrier."""
    dut = make_dut()
    a_rfm = dut.addr_vec(
        PseudoChannel=0, Sid=dut.ALL, BankGroup=dut.ALL, Bank=dut.ALL, Row=0, Column=0
    )
    a_act = _addr(dut, sid=0, bankgroup=0, bank=0, row=0)

    dut.issue("RFMab", a_rfm, clk=0)
    expected = _adj(dut.timings["nRFMab"], _HALF_TICKS, _ACT_TICKS)
    dut.assert_earliest_ready_at("ACT", a_act, expected)


def test_hbm4_rfmab_to_rfmpb_uses_nrfmab():
    """RFMab → RFMpb in the same PC: tRFMab barrier (mirrors REFab → REFpb)."""
    dut = make_dut()
    a_rfmab = dut.addr_vec(
        PseudoChannel=0, Sid=dut.ALL, BankGroup=dut.ALL, Bank=dut.ALL, Row=0, Column=0
    )
    a_rfmpb = _refpb_addr(dut, sid=0, bankgroup=0, bank=0)

    dut.issue("RFMab", a_rfmab, clk=0)
    expected = _adj(dut.timings["nRFMab"], _HALF_TICKS, _HALF_TICKS)
    dut.assert_earliest_ready_at("RFMpb", a_rfmpb, expected)


# ─────────────────────────────────────────────────────────────────────
# RDA / WRA same-bank → REFpb tail timing
# ─────────────────────────────────────────────────────────────────────

# Note on RDA / WRA → REFpb same bank: the Bank-scope edges (nRTP + nRP
# for RDA, nCWL + nBL + nWR + nRP for WRA) exist in HBM4, but the
# Bank-scope ACT → REFpb (nRC) constraint reaching back to the bank's
# original ACT dominates them on every preset (nRC > the RDA/WRA tails
# by construction). The Bank-scope RDA/WRA → REFpb edge is still
# important: it's what protects same-bank refresh on a *long-running*
# row that wasn't activated recently, where ACT→REFpb has already
# elapsed. Pinning that scenario robustly requires advancing the clock
# past nRC between ACT and RDA/WRA, which is out of scope for this
# constraint-pinning file — it's already covered indirectly in
# controller_scheduling/HBMController/.
