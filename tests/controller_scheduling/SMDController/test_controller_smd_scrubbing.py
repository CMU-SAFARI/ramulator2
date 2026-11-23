import pytest
import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

ROWS_PER_SUBARRAY = 1024
ROWS_PER_BANK = 1 << 16  # DDR4_8Gb_x8
NUM_SUBARRAYS = ROWS_PER_BANK // ROWS_PER_SUBARRAY
LATENCY = 20

INTERVAL = 81
SCRUB_SUBARRAY = NUM_SUBARRAYS // 2

def make_dram():
    return ramulator.dram.DDR4_SMD(
        org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1, rows_per_subarray=ROWS_PER_SUBARRAY)

def scrubbing(**params):
    return ramulator.smd_mechanism.ECCScrubbing(**{
        "scrubbing_period": INTERVAL * ROWS_PER_BANK,
        "num_scrubbing_machines": 16,
        "single_scrubbing_latency": LATENCY,
        "scrubbing_granularity": 1,
        **params,
    })

def make_dut(mechanisms=None, **params):
    if mechanisms is None:
        mechanisms = [scrubbing(**params)]
    return cs.ControllerUnderTest.make_smd(make_dram(), smd_mechanisms=mechanisms)

def av(dut, *, subarray=SCRUB_SUBARRAY, row=5, bank=0, bankgroup=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank, Subarray=subarray, Row=row, Column=0)

def tick_n(dut, n):
    for _ in range(n):
        dut.tick()

def read_when_scrubbing(dut, **where):
    tick_n(dut, INTERVAL - 1)
    dut.send_request("Read", av(dut, **where))
    dut.run_until_idle(max_ticks=1024)
    return dut.stats()

def test_scrubbing_counts_operations_and_rows_separately_from_refresh():
    dut = make_dut()
    tick_n(dut, 3 * INTERVAL)
    stats = dut.stats()
    assert stats["smd_scrub_ops"] == 16 * 3
    assert stats["smd_rows_scrubbed"] == 16 * 3
    assert stats["smd_maintenance_ops"] == 0
    assert stats["smd_rows_refreshed"] == 0

def test_granularity_scrubs_several_rows_per_lock():
    dut = make_dut(scrubbing_granularity=4)
    tick_n(dut, 4 * INTERVAL)
    stats = dut.stats()
    assert stats["smd_scrub_ops"] == 16
    assert stats["smd_rows_scrubbed"] == 16 * 4

def test_scrubbing_locks_the_whole_bank_by_default():
    dut = make_dut()
    assert read_when_scrubbing(make_dut(), subarray=0)["smd_act_nacks"] >= 1
    assert read_when_scrubbing(make_dut(num_scrubbing_machines=1), bank=1)["smd_act_nacks"] == 0

def test_region_lock_nacks_only_the_scrubbed_subarray():
    dut = make_dut(scrubbing_lock_region="region")
    assert read_when_scrubbing(dut, subarray=0)["smd_act_nacks"] == 0

    dut = make_dut(scrubbing_lock_region="region")
    stats = read_when_scrubbing(dut, subarray=SCRUB_SUBARRAY)
    assert stats["smd_act_nacks"] >= 1
    assert stats["num_read_reqs_served"] == 1

def test_scrub_waits_for_a_subarray_with_an_open_row():
    dut = make_dut(scrubbing_lock_region="region")
    dut.send_request("Read", av(dut))
    dut.run_until_idle(max_ticks=256)
    tick_n(dut, INTERVAL)
    assert dut.stats()["smd_scrub_ops"] < 16

def open_row_in_another_subarray(dut):
    dut.send_request("Read", av(dut, subarray=0))
    dut.run_until_idle(max_ticks=256)

def test_bank_scrub_waits_for_a_row_open_in_any_subarray_of_the_bank():
    dut = make_dut(num_scrubbing_machines=16)
    open_row_in_another_subarray(dut)
    tick_n(dut, 3 * INTERVAL)
    assert dut.stats()["smd_scrub_ops"] == 15 * 3

def test_default_scrubbing_period_is_five_minutes():
    # 5 min at the 1600 MHz DDRT-3200 clock: one row per bank every period / (rows per bank)
    interval = 480_000_000_000 // ROWS_PER_BANK

    def ops_after(ticks):
        dut = make_dut(mechanisms=[ramulator.smd_mechanism.ECCScrubbing(num_scrubbing_machines=16)])
        tick_n(dut, ticks)
        return dut.stats()["smd_scrub_ops"]

    assert ops_after(interval - 1) == 0
    assert ops_after(interval + 1) == 16

def test_scrubbing_and_refresh_share_the_lock_table():
    dut = make_dut(mechanisms=[
        ramulator.smd_mechanism.FixedRateRefresh(
            refresh_period=INTERVAL * ROWS_PER_BANK, num_ref_machines=16, single_ref_latency=LATENCY,
            row_refresh_granularity=1),
        scrubbing(scrubbing_period=8 * INTERVAL * ROWS_PER_BANK),
    ])
    tick_n(dut, 40 * INTERVAL)
    stats = dut.stats()
    assert stats["smd_scrub_ops"] > 0 and stats["smd_maintenance_ops"] > 0

def test_invalid_lock_region_is_rejected():
    with pytest.raises(Exception):
        make_dut(scrubbing_lock_region="chip")