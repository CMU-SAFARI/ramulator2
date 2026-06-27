import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


def test_device_under_test_example_flow():
    # Build a normal DRAM object first, then wrap it in DeviceUnderTest.
    dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
    dut = device_timings.DeviceUnderTest(dram)

    # Named address construction keeps short protocol tests readable.
    a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

    # A closed-bank read is functionally blocked until the row is opened.
    closed = dut.probe("RD", a, clk=0)
    # Without ACT issued, the prerequisite is ACT.
    assert closed.preq == "ACT"
    # Here we only check the timing constraints. The timing is OK here since no ACT has been issued yet!
    assert closed.timing_OK is True
    # ready means the command is fully issuable now: correct prerequisite and timing_OK.
    assert closed.ready is False

    # Open the row at cycle 0.
    dut.issue("ACT", a, clk=0)

    # Before nRCD, the row state is correct for RD but timing still blocks it.
    early = dut.probe("RD", a, clk=dut.timings["nRCD"] - 1)
    assert early.preq == "RD"
    assert early.timing_OK is False
    assert early.ready is False
    assert early.row_hit is True
    assert early.row_open is True

    # At nRCD, the same command becomes legal.
    ontime = dut.probe("RD", a, clk=dut.timings["nRCD"])
    assert ontime.preq == "RD"
    assert ontime.timing_OK is True
    assert ontime.ready is True

    # The actual read can now issue.
    dut.issue("RD", a, clk=dut.timings["nRCD"])

    # The "probe at clk-1 is blocked, probe at clk is ready" pair is the
    # canonical "this timing gate is tight" check. assert_earliest_ready_at
    # bundles both probes into a single self-describing assertion.
    # Here: PREpb after RD must wait for both nRTP (RD→PRE) and nRAS (ACT→PRE).
    t_pre = max(
        dut.timings["nRCD"] + dut.timings["nRTP"],
        dut.timings["nRAS"],
    )
    dut.assert_earliest_ready_at("PREpb", a, t_pre)
    dut.issue("PREpb", a, clk=t_pre)
