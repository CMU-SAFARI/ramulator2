"""Device-level timing tests for DDR4_PUM."""

import pytest

import ramulator
import tests.device_timings.harness as device_timings

pytestmark = pytest.mark.device_timings

PUM_COMMANDS = ["AAP", "AP", "GB_MOV", "LC_MOV", "PMAJ3", "PNOT"]


def make_dut(*, rows_per_subarray=None, channel_id=0, **overrides):
    """DDR4_PUM 8Gb x8 @ 2400R, rank=1.

    Without rows_per_subarray the bank has a single subarray, i.e. stock DDR4
    geometry.  8Gb x8 has 2**16 rows per bank, so rows_per_subarray=1024 gives
    64 subarrays.
    """
    kwargs = {
        "org_preset": "DDR4_8Gb_x8",
        "timing_preset": "DDR4_2400R",
        "rank": 1,
        **overrides,
    }
    if rows_per_subarray is not None:
        kwargs["rows_per_subarray"] = rows_per_subarray
    dram = ramulator.dram.DDR4_PUM(**kwargs)
    return device_timings.DeviceUnderTest(dram, channel_id=channel_id)


def addr(dut, *, subarray=0, row=12, bank=0, bankgroup=0, column=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank,
                        Subarray=subarray, Row=row, Column=column)


def lat_aap(t):
    return t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4


def lat_ap(t):
    return t["nRAS"] + t["nRP"]


def lat_gb_mov(t):
    return t["nRAS"] + t["nRELOC"] + t["nWR"] + t["nRP"]


def lat_lc_mov(t):
    return 2 * (t["nRAS"] + t["nRP"]) + t["nRELOC"] + t["nWR"]


# --------------------------------------------------------------------------
# 1. Subarray level is inert at subarray=1
# --------------------------------------------------------------------------

def test_stock_ddr4_traffic_is_unaffected():
    """ACT->RD still gates on nRCD with a Subarray level present."""
    dut = make_dut()
    a = addr(dut)

    dut.issue("ACT", a, clk=0)

    assert dut.probe("RD", a, clk=dut.timings["nRCD"] - 1).timing_OK is False
    assert dut.probe("RD", a, clk=dut.timings["nRCD"]).ready is True


def test_rd_on_closed_bank_still_requires_act():
    dut = make_dut()
    probe = dut.probe("RD", addr(dut), clk=0)
    assert probe.preq == "ACT"
    assert probe.row_open is False


def test_geometry_matches_stock_ddr4_when_single_subarray():
    """Bank capacity is unchanged; the subarray dimension is just 1 wide."""
    pum = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8",
                                  timing_preset="DDR4_2400R", rank=1)
    stock = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8",
                                timing_preset="DDR4_2400R", rank=1)
    pum_org, pum_timing = pum.resolve()
    stock_org, stock_timing = stock.resolve()

    assert pum_org["subarray"] == 1
    assert pum_org["row"] == stock_org["row"]
    assert pum_org["bank"] == stock_org["bank"]
    for key, value in stock_timing.items():
        assert pum_timing[key] == value, key


def test_rows_per_subarray_preserves_bank_capacity():
    pum = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8",
                                  timing_preset="DDR4_2400R", rank=1,
                                  rows_per_subarray=1024)
    org, _ = pum.resolve()
    assert org["subarray"] == 64
    assert org["row"] == 1024
    assert org["subarray"] * org["row"] == 1 << 16


@pytest.mark.parametrize("rows_per_subarray", [0, -8, 3, 1000])
def test_bad_rows_per_subarray_is_rejected(rows_per_subarray):
    dram = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8",
                                   timing_preset="DDR4_2400R", rank=1,
                                   rows_per_subarray=rows_per_subarray)
    with pytest.raises(ValueError):
        dram.resolve()


# --------------------------------------------------------------------------
# 2. PUM command semantics
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_pum_on_closed_bank_is_ready_immediately(cmd):
    dut = make_dut(rows_per_subarray=1024)
    probe = dut.probe(cmd, addr(dut), clk=0)
    assert probe.preq == cmd
    assert probe.timing_OK is True
    assert probe.ready is True


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_pum_requires_bank_closed(cmd):
    """An open CPU row forces a precharge before any PUM command can issue."""
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut)
    dut.issue("ACT", a, clk=0)
    assert dut.probe(cmd, a, clk=dut.timings["nRC"]).preq == "PREpb"


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_pum_leaves_bank_closed(cmd):
    """PUM commands end with a precharge, so a following RD needs a fresh ACT."""
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut)
    dut.issue(cmd, a, clk=0)
    assert dut.probe("RD", a, clk=10_000).preq == "ACT"


# --------------------------------------------------------------------------
# 3. Two-layer timing: same subarray serialises, different subarrays pipeline
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cmd,latency_fn", [
    ("AAP", lat_aap),
    ("AP", lat_ap),
    ("GB_MOV", lat_gb_mov),
    ("LC_MOV", lat_lc_mov),
    ("PMAJ3", lat_aap),
    ("PNOT", lat_ap),
])
def test_same_subarray_serialises_on_full_latency(cmd, latency_fn):
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut, subarray=0)
    expected = latency_fn(dut.timings)

    dut.issue(cmd, a, clk=0)

    assert dut.probe(cmd, a, clk=expected - 1).timing_OK is False
    assert dut.probe(cmd, a, clk=expected).timing_OK is True


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_different_subarray_same_bank_pipelines_at_nccdm(cmd):
    dut = make_dut(rows_per_subarray=1024)
    first = addr(dut, subarray=0)
    second = addr(dut, subarray=1)
    nccdm = dut.timings["nCCDM"]

    dut.issue(cmd, first, clk=0)

    assert dut.probe(cmd, second, clk=nccdm - 1).timing_OK is False
    assert dut.probe(cmd, second, clk=nccdm).timing_OK is True


def test_cross_subarray_is_much_cheaper_than_same_subarray():
    dut = make_dut(rows_per_subarray=1024)
    assert dut.timings["nCCDM"] < lat_aap(dut.timings) // 4


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_different_bank_is_unconstrained(cmd):
    """PUM commands are not subject to nRRDS/nRRDL."""
    dut = make_dut(rows_per_subarray=1024)
    dut.issue(cmd, addr(dut, bank=0), clk=0)
    assert dut.probe(cmd, addr(dut, bank=1), clk=0).timing_OK is True


# --------------------------------------------------------------------------
# 4. Interaction with refresh and CPU traffic
# --------------------------------------------------------------------------

@pytest.mark.parametrize("cmd,latency_fn", [
    ("AAP", lat_aap), ("AP", lat_ap),
    ("GB_MOV", lat_gb_mov), ("LC_MOV", lat_lc_mov),
    ("PMAJ3", lat_aap), ("PNOT", lat_ap),
])
def test_pum_must_drain_before_refab(cmd, latency_fn):
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut)
    expected = latency_fn(dut.timings)

    dut.issue(cmd, a, clk=0)

    ref = dut.addr_vec(Rank=0)
    assert dut.probe("REFab", ref, clk=expected - 1).timing_OK is False
    assert dut.probe("REFab", ref, clk=expected).timing_OK is True


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_refab_blocks_pum_for_nrfc(cmd):
    dut = make_dut(rows_per_subarray=1024)
    nrfc = dut.timings["nRFC"]

    dut.issue("REFab", dut.addr_vec(Rank=0), clk=0)

    a = addr(dut)
    assert dut.probe(cmd, a, clk=nrfc - 1).timing_OK is False
    assert dut.probe(cmd, a, clk=nrfc).timing_OK is True


@pytest.mark.parametrize("cmd,latency_fn", [
    ("AAP", lat_aap), ("AP", lat_ap),
    ("GB_MOV", lat_gb_mov), ("LC_MOV", lat_lc_mov),
    ("PMAJ3", lat_aap), ("PNOT", lat_ap),
])
def test_cpu_act_waits_for_pum_in_same_subarray(cmd, latency_fn):
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut, subarray=0)
    expected = latency_fn(dut.timings)

    dut.issue(cmd, a, clk=0)

    assert dut.probe("ACT", a, clk=expected - 1).timing_OK is False
    assert dut.probe("ACT", a, clk=expected).timing_OK is True


# --------------------------------------------------------------------------
# 5. Row state is per-subarray
# --------------------------------------------------------------------------

def test_same_row_index_in_different_subarrays_is_not_a_row_hit():
    dut = make_dut(rows_per_subarray=1024)
    dut.issue("ACT", addr(dut, subarray=0, row=5), clk=0)

    at = dut.timings["nRCD"]
    assert dut.probe("RD", addr(dut, subarray=0, row=5), clk=at).row_hit is True
    assert dut.probe("RD", addr(dut, subarray=1, row=5), clk=at).row_hit is False
    assert dut.probe("RD", addr(dut, subarray=9, row=5), clk=at).row_hit is False


def test_activating_another_subarray_needs_a_precharge():
    """One open row per bank."""
    dut = make_dut(rows_per_subarray=1024)
    dut.issue("ACT", addr(dut, subarray=0, row=5), clk=0)
    assert dut.probe("ACT", addr(dut, subarray=1, row=7),
                     clk=dut.timings["nRC"]).preq == "PREpb"


def test_precharge_closes_every_subarray():
    dut = make_dut(rows_per_subarray=1024)
    dut.issue("ACT", addr(dut, subarray=3, row=11), clk=0)
    dut.issue("PREpb", addr(dut, subarray=3, row=11), clk=dut.timings["nRAS"])

    at = dut.timings["nRAS"] + dut.timings["nRP"]
    assert dut.probe("RD", addr(dut, subarray=3, row=11), clk=at).row_hit is False
    assert dut.probe("ACT", addr(dut, subarray=3, row=11), clk=at).preq == "ACT"


def test_pum_command_closes_every_subarray():
    """Compute commands end with a precharge, so no subarray stays open."""
    dut = make_dut(rows_per_subarray=1024)
    dut.issue("ACT", addr(dut, subarray=2, row=4), clk=0)
    dut.issue("PREpb", addr(dut, subarray=2, row=4), clk=dut.timings["nRAS"])
    dut.issue("AAP", addr(dut, subarray=2, row=0),
              clk=dut.timings["nRAS"] + dut.timings["nRP"])

    assert dut.probe("RD", addr(dut, subarray=2, row=4), clk=10_000).row_hit is False


def test_single_subarray_matches_stock_row_behaviour():
    dut = make_dut()
    dut.issue("ACT", addr(dut, subarray=0, row=5), clk=0)
    at = dut.timings["nRCD"]
    assert dut.probe("RD", addr(dut, subarray=0, row=5), clk=at).row_hit is True
    assert dut.probe("RD", addr(dut, subarray=0, row=6), clk=at).row_hit is False


# --------------------------------------------------------------------------
# 6. Gating inherited from ACT, and the bank being busy during a PUM command
# --------------------------------------------------------------------------

LATENCIES = [
    ("AAP", lat_aap), ("AP", lat_ap),
    ("GB_MOV", lat_gb_mov), ("LC_MOV", lat_lc_mov),
    ("PMAJ3", lat_aap), ("PNOT", lat_ap),
]


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_pum_waits_for_rda_auto_precharge(cmd):
    dut = make_dut(rows_per_subarray=1024)
    t = dut.timings
    a = addr(dut)
    dut.issue("ACT", a, clk=0)
    rda = t["nRCD"] + 4 * t["nCCDL"]
    dut.issue("RDA", a, clk=rda)

    done = rda + t["nRTP"] + t["nRP"]
    assert dut.probe(cmd, a, clk=done - 1).timing_OK is False
    assert dut.probe(cmd, a, clk=done).timing_OK is True


@pytest.mark.parametrize("cmd", PUM_COMMANDS)
def test_pum_waits_for_wra_auto_precharge(cmd):
    dut = make_dut(rows_per_subarray=1024)
    t = dut.timings
    a = addr(dut)
    dut.issue("ACT", a, clk=0)
    dut.issue("WRA", a, clk=t["nRCD"])

    done = t["nRCD"] + t["nCWL"] + t["nBL"] + t["nWR"] + t["nRP"]
    assert dut.probe(cmd, a, clk=done - 1).timing_OK is False
    assert dut.probe(cmd, a, clk=done).timing_OK is True


@pytest.mark.parametrize("cmd,latency_fn", LATENCIES)
def test_cpu_act_to_another_subarray_waits_for_pum(cmd, latency_fn):
    dut = make_dut(rows_per_subarray=1024)
    expected = latency_fn(dut.timings)

    dut.issue(cmd, addr(dut, subarray=0), clk=0)

    other = addr(dut, subarray=1)
    assert dut.probe("ACT", other, clk=expected - 1).timing_OK is False
    assert dut.probe("ACT", other, clk=expected).timing_OK is True


@pytest.mark.parametrize("cmd,latency_fn", LATENCIES)
def test_precharge_waits_for_pum(cmd, latency_fn):
    dut = make_dut(rows_per_subarray=1024)
    a = addr(dut)
    expected = latency_fn(dut.timings)

    dut.issue(cmd, a, clk=0)

    assert dut.probe("PREpb", a, clk=expected - 1).timing_OK is False
    assert dut.probe("PREpb", a, clk=expected).timing_OK is True
    assert dut.probe("PREab", a, clk=expected - 1).timing_OK is False
    assert dut.probe("PREab", a, clk=expected).timing_OK is True


def test_pum_commands_are_not_subject_to_nfaw():
    """More than four activations may start within nFAW."""
    dut = make_dut(rows_per_subarray=1024)
    for i in range(8):
        a = addr(dut, bankgroup=i % 4, bank=i // 4)
        assert dut.probe("AAP", a, clk=i).timing_OK is True
        dut.issue("AAP", a, clk=i)
    assert dut.probe("ACT", addr(dut, bankgroup=0, bank=2), clk=8).timing_OK is True


def test_nccdm_follows_nrrdl_override():
    dram = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R",
                                   rank=1, nRRDL=9)
    assert dram.resolve()[1]["nCCDM"] == 9
    dram = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R",
                                   rank=1, nRRDL=9, nCCDM=3)
    assert dram.resolve()[1]["nCCDM"] == 3
