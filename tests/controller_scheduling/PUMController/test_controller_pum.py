"""Controller-scheduling tests for the PUM controller."""

import pytest

import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

PUM_REQUESTS = [
    ("PUM_AAP", "AAP"),
    ("PUM_AP", "AP"),
    ("PUM_GB_MOV", "GB_MOV"),
    ("PUM_LC_MOV", "LC_MOV"),
    ("PUM_PMAJ3", "PMAJ3"),
    ("PUM_PNOT", "PNOT"),
]


def make_dram(**kwargs):
    return ramulator.dram.DDR4_PUM(**{
        "org_preset": "DDR4_8Gb_x8",
        "timing_preset": "DDR4_2400R",
        "rank": 1,
        "rows_per_subarray": 1024,
        **kwargs,
    })


def make_dut(**kwargs):
    return cs.ControllerUnderTest.make_pum(make_dram(), **kwargs)


def av(dut, *, subarray=0, row=0, bank=0, bankgroup=0, column=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank,
                        Subarray=subarray, Row=row, Column=column)


# --------------------------------------------------------------------------
# Basic acceptance and issue
# --------------------------------------------------------------------------

@pytest.mark.parametrize("req_type,command", PUM_REQUESTS)
def test_pum_request_issues_its_command(req_type, command):
    dut = make_dut()
    dut.send_request(req_type, av(dut))
    history = dut.run_until_idle(max_ticks=512)
    dut.assert_commands([command], history=history)


@pytest.mark.parametrize("req_type,command", PUM_REQUESTS)
def test_pum_request_is_counted_and_served(req_type, command):
    dut = make_dut()
    dut.send_request(req_type, av(dut))
    dut.run_until_idle(max_ticks=512)

    stats = dut.stats()
    assert stats[f"num_pum_{command}_reqs"] == 1
    assert stats[f"num_pum_{command}_reqs_served"] == 1
    assert stats["num_pum_reqs"] == 1
    assert stats["num_pum_reqs_served"] == 1
    # Compute requests must not be miscounted as ordinary traffic.
    assert stats["num_read_reqs"] == 0
    assert stats["num_write_reqs"] == 0


def test_read_and_write_still_work():
    dut = make_dut()
    dut.send_request("Read", av(dut, row=7))
    history = dut.run_until_idle(max_ticks=512)

    dut.assert_commands(["ACT", "RD"], history=history)
    assert dut.stats()["num_read_reqs"] == 1
    assert dut.stats()["num_pum_reqs"] == 0


# --------------------------------------------------------------------------
# Interaction with open CPU rows
# --------------------------------------------------------------------------

@pytest.mark.parametrize("req_type,command", PUM_REQUESTS)
def test_open_row_forces_precharge_before_pum(req_type, command):
    """A PUM command needs the bank closed, so the controller injects PREpb."""
    dut = make_dut(row_policy=ramulator.row_policy.Open())

    dut.send_request("Read", av(dut, row=7))
    dut.run_until_idle(max_ticks=512)
    assert dut.history[-1].command == "RD"  # row left open

    start = len(dut.history)
    dut.send_request(req_type, av(dut, row=0))
    history = dut.run_until_idle(max_ticks=512)

    assert [h.command for h in history] == ["PREpb", command], (
        f"expected a precharge before {command}, got {[h.command for h in history]}")
    assert start > 0


def test_pum_does_not_preempt_an_active_cpu_request():
    """A compute command must not close a bank with a CPU request in flight."""
    dut = make_dut()
    # Send the read first so its ACT is issued and it sits in the active buffer,
    # then queue a PUM command to the same bank.
    dut.send_request("Read", av(dut, row=7))
    dut.tick()  # issues ACT, read promotes to active
    dut.send_request("PUM_AAP", av(dut, row=0))

    history = dut.run_until_idle(max_ticks=512)
    commands = [h.command for h in history]

    # The read completes before the compute command closes the bank.
    assert commands.index("RD") < commands.index("AAP"), commands


# --------------------------------------------------------------------------
# Timing: the two-layer subarray model, observed through the controller
# --------------------------------------------------------------------------

def test_same_subarray_commands_serialise():
    dut = make_dut()
    t = dut.timings
    expected = t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4

    dut.send_request("PUM_AAP", av(dut, subarray=0))
    dut.send_request("PUM_AAP", av(dut, subarray=0))
    history = dut.run_until_idle(max_ticks=1024)

    dut.assert_commands(["AAP", "AAP"], history=history)
    dut.assert_gap(0, 1, expected, history=history)


def test_different_subarrays_pipeline_at_nccdm():
    dut = make_dut()

    dut.send_request("PUM_AAP", av(dut, subarray=0))
    dut.send_request("PUM_AAP", av(dut, subarray=1))
    history = dut.run_until_idle(max_ticks=1024)

    dut.assert_commands(["AAP", "AAP"], history=history)
    dut.assert_gap(0, 1, dut.timings["nCCDM"], history=history)


def test_subarray_parallelism_beats_serialisation():
    dut = make_dut()
    t = dut.timings
    same = t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4
    assert t["nCCDM"] * 4 < same


def test_different_banks_issue_back_to_back():
    """PUM commands are not subject to nRRDS/nRRDL."""
    dut = make_dut()

    dut.send_request("PUM_AAP", av(dut, bank=0))
    dut.send_request("PUM_AAP", av(dut, bank=1))
    history = dut.run_until_idle(max_ticks=1024)

    dut.assert_commands(["AAP", "AAP"], history=history)
    dut.assert_gap(0, 1, 1, history=history)


def test_occupancy_reflects_the_spec_timing_table():
    """The controller must read latency out of the spec, not recompute it."""
    dut = make_dut()
    t = dut.timings

    dut.send_request("PUM_AAP", av(dut))
    dut.send_request("PUM_LC_MOV", av(dut, subarray=1))
    dut.run_until_idle(max_ticks=1024)

    expected = ((t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4)
                + (2 * (t["nRAS"] + t["nRP"]) + t["nRELOC"] + t["nWR"]))
    assert dut.stats()["pum_occupancy_cycles"] == expected


def test_cpu_read_to_another_subarray_waits_for_pum():
    dut = make_dut()
    t = dut.timings
    latency = t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4

    dut.send_request("PUM_AAP", av(dut, subarray=0))
    dut.send_request("Read", av(dut, subarray=1, row=7))
    history = dut.run_until_idle(max_ticks=1024)

    dut.assert_commands(["AAP", "ACT", "RD"], history=history)
    dut.assert_gap(0, 1, latency, history=history)


@pytest.mark.parametrize("row_policy", [
    ramulator.row_policy.Open(),
    ramulator.row_policy.ClosedCAP(cap=2),
], ids=["Open", "ClosedCAP"])
def test_read_traffic_matches_generic_ddr(row_policy):
    """Without compute requests, PUM schedules exactly like GenericDDR."""
    pattern = [(bank, subarray, row) for row in (3, 9) for bank in range(4) for subarray in (0, 5)]
    histories = []
    for make in (cs.ControllerUnderTest.make_generic_ddr, cs.ControllerUnderTest.make_pum):
        dut = make(make_dram(), row_policy=row_policy)
        for bank, subarray, row in pattern:
            dut.send_request("Read", av(dut, bank=bank, subarray=subarray, row=row))
        history = dut.run_until_idle(max_ticks=4096)
        histories.append([(h.clk, h.command, h.addr_vec) for h in history])
    assert len(histories[0]) > len(pattern)
    assert histories[0] == histories[1]


# --------------------------------------------------------------------------
# Configuration errors
# --------------------------------------------------------------------------

def test_rejects_standard_without_subarray_level():
    plain = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8",
                                timing_preset="DDR4_2400R", rank=1)
    with pytest.raises(Exception, match="Subarray"):
        cs.ControllerUnderTest.make_pum(plain)
