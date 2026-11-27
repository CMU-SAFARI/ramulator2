"""Controller-scheduling tests for Self-Managing DRAM (SMD): fixed-rate refresh, ACT_NACK and retry."""

import math
import pytest
import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

ROWS_PER_SUBARRAY = 1024
ROWS_PER_BANK = 1 << 16
LATENCY = 80
GRANULARITY = 8

INTERVAL = 81 * GRANULARITY
BUSY = LATENCY * GRANULARITY

REFRESH_PARAMS = {
    "refresh_period": 81 * ROWS_PER_BANK,
    "num_ref_machines": 16,
    "single_ref_latency": LATENCY,
    "row_refresh_granularity": GRANULARITY,
}

def make_dram():
    return ramulator.dram.DDR4_SMD(
        org_preset="DDR4_8Gb_x8",
        timing_preset="DDR4_2400R",
        rank=1,
        rows_per_subarray=ROWS_PER_SUBARRAY,
    )

def fixed_rate_refresh(**params):
    return ramulator.smd_mechanism.FixedRateRefresh(**{**REFRESH_PARAMS, **params})

def make_dut(mechanisms=None, **refresh_params):
    """SMD controller. By default one fixed-rate refresh mechanism, turned by `refresh_params`. 
       `mechanisms` replace it (an empty list: the DRAM maintains nothing)."""
    if mechanisms is None:
        mechanisms = [fixed_rate_refresh(**refresh_params)]
    return cs.ControllerUnderTest.make_smd(make_dram(), smd_mechanisms=mechanisms)

def av(dut, *, subarray=0, row=5, bank=0, bankgroup=0, column=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank, Subarray=subarray, Row=row, Column=column)

def tick_n(dut, n):
    for _ in range(n):
        dut.tick()

def tick_until_lock(dut):
    """Idle until the cycle before the first maintenance operation starts."""
    tick_n(dut, INTERVAL - 1)

def commands(history):
    return [h.command for h in history]

# -------------------------------------------------------------------
# ACT_NACK and retry
# -------------------------------------------------------------------

def test_access_before_any_maintenance_is_not_nacked():
    dut = make_dut()
    dut.send_request("Read", av(dut))
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD"], history=history)
    assert dut.stats()["smd_act_nacks"] == 0

def test_act_to_a_locked_subarray_is_nacked_until_maintenance_ends():
    dut = make_dut()
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    history = dut.run_until_idle(max_ticks=4 * BUSY)

    nack_to_act = dut.timing("nACTtoNACK") + dut.timing("nNACK_RESEND")
    num_nacks = math.ceil(BUSY / nack_to_act)
    assert num_nacks >= 2
    dut.assert_commands(["ACT", "ACT_NACK"] * num_nacks + ["ACT", "RD"], history=history)
    assert dut.stats()["smd_act_nacks"] == num_nacks

    acts = [h for h in history if h.command == "ACT"]
    nacks = [h for h in history if h.command == "ACT_NACK"]
    for act, nack in zip(acts, nacks):
        assert nack.clk - act.clk == dut.timing("nACTtoNACK")
    for nack, retry in zip(nacks, acts[1:]):
        assert retry.clk - nack.clk == dut.timing("nNACK_RESEND")
    assert acts[-1].clk - acts[0].clk >= BUSY
    assert history[-1].clk - acts[-1].clk == dut.timing("nRCD")

def test_nacked_request_completes_after_the_lock_is_released():
    dut = make_dut()
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    dut.run_until_idle(max_ticks=4 * BUSY)

    assert dut.stats()["num_read_reqs_served"] == 1

def test_another_subarray_of_the_locked_bank_is_not_nacked():
    dut = make_dut()
    tick_until_lock(dut)

    dut.send_request("Read", av(dut, subarray=1))
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD"], history=history)
    assert dut.stats()["smd_act_nacks"] == 0

def test_nack_of_one_bank_does_not_block_other_banks():
    dut = make_dut()
    tick_until_lock(dut)

    dut.send_request("Read", av(dut, bank=0, subarray=0))
    dut.send_request("Read", av(dut, bank=1, subarray=1))
    history = dut.run_until_idle(max_ticks=4 * BUSY)

    ok_act = next(h for h in history if h.command == "ACT" and h.addr_vec[dut.level_names.index("Bank")] == 1)
    first_nack = next(h for h in history if h.command == "ACT_NACK")
    assert ok_act.clk < first_nack.clk + dut.timing("nNACK_RESEND")
    assert dut.stats()["num_read_reqs_served"] == 2

def test_a_second_request_does_not_take_the_bank_of_a_nacked_one():
    dut = make_dut()
    tick_until_lock(dut)

    dut.send_request("Read", av(dut, subarray=0, row=5))
    dut.send_request("Read", av(dut, subarray=0, row=6))
    dut.run_until_idle(max_ticks=8 * BUSY)

    assert dut.stats()["num_read_reqs_served"] == 2

def test_stream_of_requests_makes_progress_through_maintenance():
    dut = make_dut()
    tick_n(dut, INTERVAL - 8)

    for row in range(20):
        dut.send_request("Read", av(dut, subarray=0, row=row))
    dut.run_until_idle(max_ticks=40 * BUSY)

    stats = dut.stats()
    assert stats["num_read_reqs_served"] == 20
    assert stats["smd_act_nacks"] >= 1

# -------------------------------------------------------------------
# Maintenance schedule
# -------------------------------------------------------------------

def test_maintenance_sweeps_one_region_per_interval():
    dut = make_dut()
    tick_n(dut, INTERVAL)
    stats = dut.stats()
    assert stats["smd_maintenance_ops"] == 16
    assert stats["smd_rows_refreshed"] == 16 * GRANULARITY

    dut = make_dut()
    tick_n(dut, 6 * INTERVAL)
    assert dut.stats()["smd_maintenance_ops"] == 16 * 6

def test_stats_reset_clears_smd_counters():
    dut = make_dut()
    tick_n(dut, 3 * INTERVAL)
    dut.reset_stats()
    tick_n(dut, INTERVAL)

    stats = dut.stats()
    assert stats["smd_maintenance_ops"] == 16
    assert stats["smd_rows_refreshed"] == 16 * GRANULARITY

def test_a_row_open_for_long_is_precharged_while_it_is_still_being_hit():
    dut = make_dut()

    for column in range(6):
        dut.send_request("Read", av(dut, subarray=0, row=5, column=column))
        dut.run_until_idle(max_ticks=4 * BUSY)
        tick_n(dut, INTERVAL)

    precharges = [h for h in dut.history if h.command == "PREpb"]
    assert len(precharges) == 1
    assert dut.stats()["smd_forced_precharges"] == 1

def test_a_row_open_for_less_than_the_limit_stays_open():
    dut = make_dut()
    a = av(dut, subarray=0, row=5)

    dut.send_request("Read", a)
    dut.run_until_idle(max_ticks=128)
    tick_n(dut, INTERVAL * 2)
    dut.send_request("Read", a)
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["RD"], history=history)
    assert dut.stats()["smd_forced_precharges"] == 0

def test_maintenance_that_cannot_keep_up_is_reported():
    dut = make_dut(max_row_open_intervals=10**6)
    dut.send_request("Read", av(dut, subarray=0, row=5))
    dut.run_until_idle(max_ticks=128)

    with pytest.raises(RuntimeError, match="cannot keep up"):
        tick_n(dut, 12 * INTERVAL)

# -------------------------------------------------------------------
# Configuration
# -------------------------------------------------------------------

def test_smd_without_mechanisms_never_nacks():
    dut = make_dut(mechanisms=[])
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD"], history=history)
    assert dut.stats()["smd_act_nacks"] == 0
    assert dut.stats()["smd_maintenance_ops"] == 0

def test_refresh_interval_must_exceed_the_maintenance_latency():
    with pytest.raises(Exception, match="must be shorter than the interval"):
        make_dut(refresh_period=ROWS_PER_BANK * LATENCY)

def test_machine_count_must_divide_the_banks():
    with pytest.raises(Exception, match="must divide"):
        make_dut(num_ref_machines=3)

def test_standard_without_act_nack_is_rejected():
    dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
    with pytest.raises(Exception, match="ACT_NACK"):
        cs.ControllerUnderTest.make_smd(dram)

# -------------------------------------------------------------------
# SMD off: DDR4_SMD behaves like DDR4
# -------------------------------------------------------------------

def _trace(make_dut_fn, requests):
    dut = make_dut_fn()
    for req_type, bankgroup, bank, row, column in requests:
        levels = dict(Rank=0, BankGroup=bankgroup, Bank=bank, Row=row, Column=column)
        if "Subarray" in dut.level_names:
            levels["Subarray"] = 0
        dut.send_request(req_type, dut.addr_vec(**levels))
    history = dut.run_until_idle(max_ticks=4096)
    return [(h.clk, h.command) for h in history]

def test_smd_without_mechanisms_schedules_like_plain_ddr4():
    # (type, bankgroup, bank, row, column)
    requests = [
        ("Read", 0, 0, 5, 0),
        ("Read", 0, 0, 5, 1),
        ("Read", 0, 0, 6, 0),
        ("Write", 0, 1, 9, 0),
        ("Read", 1, 2, 1, 0),
        ("Write", 0, 1, 9, 1),
    ]

    def smd():
        dram = ramulator.dram.DDR4_SMD(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
        return cs.ControllerUnderTest.make_smd(dram, smd_mechanisms=[])

    def plain():
        dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
        return cs.ControllerUnderTest.make_generic_ddr(dram)

    assert _trace(smd, requests) == _trace(plain, requests)
    
# -------------------------------------------------------------------
# Variable-rate refresh (SMD-VR)
# -------------------------------------------------------------------

BIG_BLOOM_FILTER = {"bloom_filter_size": 1 << 20}

def make_vr_dut(weak_row_percentage, **params):
    return make_dut(mechanisms=[
        ramulator.smd_mechanism.VariableRefresh(
            **{**REFRESH_PARAMS, "weak_row_percentage": weak_row_percentage, **params}),
    ])

def test_vr_without_weak_rows_defers_all_maintenance():
    dut = make_vr_dut(0.0)
    tick_n(dut, 6 * INTERVAL)
    assert dut.stats()["smd_maintenance_ops"] == 0

def test_vr_without_weak_rows_never_nacks():
    dut = make_vr_dut(0.0)
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    history = dut.run_until_idle(max_ticks=128)

    dut.assert_commands(["ACT", "RD"], history=history)

def test_vr_with_only_weak_rows_refreshes_like_fixed_rate():
    vr = make_vr_dut(100.0)
    tick_n(vr, 6 * INTERVAL)
    fr = make_dut()
    tick_n(fr, 6 * INTERVAL)

    assert vr.stats()["smd_maintenance_ops"] == fr.stats()["smd_maintenance_ops"] == 16 * 6
    assert vr.stats()["smd_rows_refreshed"] == fr.stats()["smd_rows_refreshed"]

def test_vr_nacks_like_fixed_rate_when_every_row_is_weak():
    dut = make_vr_dut(100.0)
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    history = dut.run_until_idle(max_ticks=4 * BUSY)

    nack_to_act = dut.timing("nACTtoNACK") + dut.timing("nNACK_RESEND")
    num_nacks = math.ceil(BUSY / nack_to_act)
    dut.assert_commands(["ACT", "ACT_NACK"] * num_nacks + ["ACT", "RD"], history=history)
    assert dut.stats()["smd_act_partial_nacks"] == 0

def test_vr_refreshes_only_the_weak_rows_of_a_window():
    dut = make_vr_dut(1.0, **BIG_BLOOM_FILTER)
    tick_n(dut, 40 * INTERVAL)

    stats = dut.stats()
    ops, rows = stats["smd_maintenance_ops"], stats["smd_rows_refreshed"]
    assert 0 < ops < 0.3 * 16 * 40
    assert ops <= rows < 0.4 * GRANULARITY * ops
    assert stats["smd_bloom_positives"] >= rows

def test_vr_chips_with_different_weak_rows_give_partial_nacks():
    dut = make_vr_dut(1.0, **BIG_BLOOM_FILTER)
    tick_until_lock(dut)

    for bankgroup in range(4):
        for bank in range(4):
            dut.send_request("Read", av(dut, bankgroup=bankgroup, bank=bank))
    dut.run_until_idle(max_ticks=40 * BUSY)

    stats = dut.stats()
    assert stats["smd_act_partial_nacks"] > 0
    assert stats["num_read_reqs_served"] == 16

def test_vr_normal_distribution_runs():
    dut = make_vr_dut(0.1, distribution="normal")
    tick_n(dut, 3 * INTERVAL)
    dut.stats()

def test_vr_rejects_unknown_distribution():
    with pytest.raises(Exception, match="distribution"):
        make_vr_dut(0.1, distribution="bogus")

def test_vr_bloom_filter_size_must_be_a_power_of_two():
    with pytest.raises(Exception, match="power of two"):
        make_vr_dut(0.1, bloom_filter_size=1000)

def test_worst_case_distribution_staggers_the_chips():
    dut = make_dut(worst_case_ref_distribution=True)
    tick_until_lock(dut)

    dut.send_request("Read", av(dut))
    dut.run_until_idle(max_ticks=40 * BUSY)

    stats = dut.stats()
    assert stats["smd_act_partial_nacks"] >= 1
    assert stats["smd_act_nacks"] == 0
    assert stats["num_read_reqs_served"] == 1

def test_an_idle_row_open_for_long_is_precharged():
    dut = make_dut()
    dut.send_request("Read", av(dut, subarray=0, row=5))
    dut.run_until_idle(max_ticks=128)
    act_clk = dut.history[0].clk

    tick_n(dut, 6 * INTERVAL)

    precharges = [h for h in dut.history if h.command == "PREpb"]
    assert len(precharges) == 1
    assert precharges[0].clk - act_clk ==INTERVAL * 4 + 1

def test_a_long_open_row_does_not_stall_maintenance_with_the_open_row_policy():
    dut = make_dut()
    dut.send_request("Read", av(dut, subarray=0, row=5))
    dut.run_until_idle(max_ticks=128)

    tick_n(dut, 30 * INTERVAL)
    assert dut.stats()["smd_maintenance_ops"] > 0
