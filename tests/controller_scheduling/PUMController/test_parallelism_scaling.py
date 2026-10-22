"""Bank- and subarray-level parallelism of PUM commands, measured at the controller.

On DDR4-2400, consecutive AAPs are 59 cycles apart in the same subarray, nCCDM
(6) apart across subarrays of one bank, and back-to-back across banks.
"""

import pytest

import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

NUM_COMMANDS = 16


def make_dut():
    dram = ramulator.dram.DDR4_PUM(
        org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R",
        rank=1, rows_per_subarray=1024)
    return cs.ControllerUnderTest.make_pum(dram)


# DDR4_8Gb_x8: 4 bank groups x 4 banks = 16 banks per rank.
BANKS_PER_GROUP = 4


def batch_span(banks, subarrays, count=NUM_COMMANDS):
    """Cycles from first to last issue for `count` AAPs spread over the given
    number of banks and subarrays, round-robin.

    `banks` counts distinct physical banks, split across bank groups since a
    DDR4 bank index only addresses banks within one group.
    """
    dut = make_dut()
    for i in range(count):
        bank = i % banks
        dut.send_request("PUM_AAP", dut.addr_vec(
            Rank=0,
            BankGroup=bank // BANKS_PER_GROUP,
            Bank=bank % BANKS_PER_GROUP,
            Subarray=((i // banks) % subarrays),
            Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=200_000)
    assert len(history) == count, f"expected {count} commands, got {len(history)}"
    return history[-1].clk - history[0].clk


def test_serial_batch_pays_full_latency_each():
    """One bank, one subarray: every command waits for the previous to finish."""
    dut = make_dut()
    t = dut.timings
    lat = t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4

    span = batch_span(banks=1, subarrays=1)

    assert span == lat * (NUM_COMMANDS - 1)


def test_subarray_parallelism_speeds_up_a_batch():
    serial = batch_span(banks=1, subarrays=1)
    parallel = batch_span(banks=1, subarrays=8)
    assert parallel < serial / 4, f"SALP=8 gave only {serial / parallel:.2f}x"


def test_bank_parallelism_speeds_up_a_batch():
    serial = batch_span(banks=1, subarrays=1)
    parallel = batch_span(banks=8, subarrays=1)
    assert parallel < serial / 4, f"BLP=8 gave only {serial / parallel:.2f}x"


def test_banks_and_subarrays_compose():
    """Using both should beat using either alone."""
    banks_only = batch_span(banks=4, subarrays=1)
    subarrays_only = batch_span(banks=1, subarrays=4)
    both = batch_span(banks=4, subarrays=4)
    assert both <= banks_only
    assert both <= subarrays_only


def test_cross_subarray_gap_is_nccdm():
    dut = make_dut()
    for subarray in (0, 1):
        dut.send_request("PUM_AAP", dut.addr_vec(
            Rank=0, BankGroup=0, Bank=0, Subarray=subarray, Row=0, Column=0))
    history = dut.run_until_idle(max_ticks=1024)
    dut.assert_gap(0, 1, dut.timings["nCCDM"], history=history)


def test_single_subarray_is_the_slow_case():
    """Spreading over subarrays never makes a batch slower."""
    serial = batch_span(banks=1, subarrays=1)
    for subarrays in (2, 4, 8, 16):
        assert batch_span(banks=1, subarrays=subarrays) <= serial
