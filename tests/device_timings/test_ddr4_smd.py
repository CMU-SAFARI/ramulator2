import pytest
import ramulator
import tests.device_timings.harness as device_timings

pytestmark = pytest.mark.device_timings

def make_dut(**overrides):
    """DDR4 SMD 8Gb x8 @ 2400R, rank=1, 64 subarrays of 1024 rows per bank."""
    dram = ramulator.dram.DDR4_SMD(**{
        "org_preset": "DDR4_8Gb_x8",
        "timing_preset": "DDR4_2400R",
        "rank": 1,
        "rows_per_subarray": 1024,
        **overrides,
    })
    return device_timings.DeviceUnderTest(dram)

def addr(dut, *, subarray=0, row=3, bank=0):
    return dut.addr_vec(Rank=0, BankGroup=0, Bank=bank, Subarray=subarray, Row=row, Column=0)

def test_smd_timings_default_to_4_cycles_and_100ns():
    dut = make_dut()
    assert dut.timings["nACTtoNACK"] == 4
    assert dut.timings["nNACK_RESEND"] == 121

def test_timing_overrides_apply():
    dut = make_dut(nACTtoNACK=2, nNACK_RESEND=50)
    assert dut.timings["nACTtoNACK"] == 2
    assert dut.timings["nNACK_RESEND"] == 50

def test_act_nack_closes_the_bank():
    dut = make_dut()
    a = addr(dut)

    dut.issue("ACT", a, clk=0)
    assert dut.probe("RD", a, clk=dut.timings["nRCD"]).row_open is True
    
    dut.issue("ACT_NACK", a, clk=dut.timings["nACTtoNACK"])
    probe = dut.probe("RD", a, clk=dut.timings["nRC"])
    assert probe.preq == "ACT"
    assert probe.row_open is False
    assert probe.row_hit is False

def test_nacked_subarray_refuses_act_for_the_resend_interval():
    dut = make_dut()
    a = addr(dut)
    nack_clk = dut.timings["nACTtoNACK"]
    resend = dut.timings["nNACK_RESEND"]

    dut.issue("ACT", a, clk=0)
    dut.issue("ACT_NACK", a, clk=nack_clk)

    early = dut.probe("ACT", a, clk=nack_clk + resend - 1)
    ontime = dut.probe("ACT", a, clk=nack_clk + resend)
    assert early.timing_OK is False
    assert ontime.timing_OK is True

def test_other_subarray_of_the_bank_is_not_held_by_the_nack():
    dut = make_dut()
    a = addr(dut, subarray=0)
    other = addr(dut, subarray=1)

    dut.issue("ACT", a, clk=0)
    dut.issue("ACT_NACK", a, clk=dut.timings["nACTtoNACK"])

    assert dut.probe("ACT", other, clk=dut.timings["nRC"]).timing_OK is True

def test_act_to_a_different_subarray_needs_the_open_one_closed():
    dut = make_dut()
    a = addr(dut, subarray=0)
    other = addr(dut, subarray=1)

    dut.issue("ACT", a, clk=0)

    probe = dut.probe("RD", other, clk=dut.timings["nRCD"])
    assert probe.preq == "PREpb"
    assert probe.row_hit is False
    assert probe.row_open is True

def test_row_hit_is_per_subarray():
    dut = make_dut()
    a = addr(dut, subarray=0, row=3)
    same_row_other_subarray = addr(dut, subarray=1, row=3)

    dut.issue("ACT", a, clk=0)

    assert dut.probe("RD", a, clk=dut.timings["nRCD"]).row_hit is True
    assert dut.probe("RD", same_row_other_subarray, clk=dut.timings["nRCD"]).row_hit is False
