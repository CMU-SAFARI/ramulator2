import pytest
import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

ROWS_PER_SUBARRAY = 1024 # a lock region
PHYSICAL_ROWS = 256      # one physical subarray: 4 per region
ROWS_PER_BANK = 1 << 16  # DDR4_8Gb_x8
NUM_SUBARRAYS = ROWS_PER_BANK // ROWS_PER_SUBARRAY
LATENCY = 20
INTERVAL = 81
LOCKED = NUM_SUBARRAYS // 2

def make_dut(**controller_params):
    mechanism = ramulator.smd_mechanism.ECCScrubbing(
        scrubbing_period=INTERVAL * ROWS_PER_BANK,
        scrubbing_lock_region="region",
        num_scrubbing_machines=16,
        single_scrubbing_latency=LATENCY,
        scrubbing_granularity=1,
    )
    dram = ramulator.dram.DDR4_SMD(
        org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1, rows_per_subarray=ROWS_PER_SUBARRAY)
    return cs.ControllerUnderTest.make_smd(dram, smd_mechanisms=[mechanism], **controller_params)

def nacks(subarray, row, **controller_params):
    """ACT_NACKs of a read to (subarray, row) of bank 0 while region LOCKED of that bank is locked."""
    dut = make_dut(**controller_params)
    for _ in range(INTERVAL - 1):
        dut.tick()
    dut.send_request("Read", dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Subarray=subarray, Row=row, Column=0))
    dut.run_until_idle(max_ticks=1024)
    return dut.stats()["smd_act_nacks"]

OPEN = {"open_bitline": True, "physical_subarray_rows": PHYSICAL_ROWS}

def test_adjacent_regions_are_not_blocked_by_default():
    assert nacks(LOCKED, 5) >= 1
    assert nacks(LOCKED + 1, 5) == 0
    assert nacks(LOCKED - 1, ROWS_PER_SUBARRAY) == 0

def test_first_physical_subarray_of_the_region_above_is_blocked():
    assert nacks(LOCKED + 1, 0, **OPEN) >= 1
    assert nacks(LOCKED + 1, PHYSICAL_ROWS - 1, **OPEN) >= 1

def test_last_physical_subarray_of_the_region_below_is_blocked():
    assert nacks(LOCKED - 1, ROWS_PER_SUBARRAY - 1, **OPEN) >= 1
    assert nacks(LOCKED - 1, ROWS_PER_SUBARRAY - PHYSICAL_ROWS, **OPEN) >= 1

def test_the_rest_of_the_adjacent_regions_is_not_blocked():
    assert nacks(LOCKED + 1, PHYSICAL_ROWS, **OPEN) == 0
    assert nacks(LOCKED - 1, ROWS_PER_SUBARRAY - PHYSICAL_ROWS - 1, **OPEN) == 0

def test_regions_further_away_are_not_blocked():
    assert nacks(LOCKED + 2, 0, **OPEN) == 0
    assert nacks(LOCKED - 2, ROWS_PER_SUBARRAY - 1, **OPEN) == 0

def test_the_locked_region_is_still_blocked():
    assert nacks(LOCKED, 500, **OPEN) >= 1

def test_a_region_of_one_physical_subarray_blocks_both_neighbors_entirely():
    big = {"open_bitline": True, "physical_subarray_rows": ROWS_PER_SUBARRAY}
    assert nacks(LOCKED + 1, 500, **big) >= 1
    assert nacks(LOCKED - 1, 500, **big) >= 1
    assert nacks(LOCKED + 2, 500, **big) == 0

def test_invalid_physical_subarray_rows_is_rejected():
    with pytest.raises(Exception):
        make_dut(open_bitline=True, physical_subarray_rows=0)