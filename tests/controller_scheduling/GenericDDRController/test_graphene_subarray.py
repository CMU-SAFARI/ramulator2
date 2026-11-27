import pytest
import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

THRESHOLD = 4

def make_dut():
    dram = ramulator.dram.DDR4_VRR_SMD(
        org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1, rows_per_subarray=1024)
    graphene = ramulator.controller_plugin.Graphene(
        num_table_entries=8,
        activation_threshold=THRESHOLD,
        reset_period_ns=64_000_000,  # no reset during the test
    )
    return cs.ControllerUnderTest.make_generic_ddr(dram, controller_plugins=[graphene])

def activate(dut, *, subarray, row):
    dut.send_request("Read", dut.addr_vec(
        Rank=0, BankGroup=0, Bank=0, Subarray=subarray, Row=row, Column=0))
    dut.run_until_idle(max_ticks=2048)

def victim_refreshes(dut):
    for _ in range(200):
        dut.tick()
    return [h for h in dut.history if h.command == "VRR"]

def test_graphene_refreshes_victims_of_a_row_hammered_in_one_subarray():
    # Control: row 5 of subarray 0 reaches the threshold (alternating with row 6 forces an ACT each time)
    dut = make_dut()
    for _ in range(THRESHOLD):
        activate(dut, subarray=0, row=5)
        activate(dut, subarray=0, row=6)

    refreshes = victim_refreshes(dut)

    assert len(refreshes) >= 1
    assert refreshes[0].addr_vec[dut.level_names.index("Row")] == 5

def test_graphene_does_not_mix_up_equal_row_number_of_different_subarrays():
    dut = make_dut()
    for _ in range(THRESHOLD // 2):
        activate(dut, subarray=0, row=5)
        activate(dut, subarray=1, row=5)

    assert victim_refreshes(dut) == []