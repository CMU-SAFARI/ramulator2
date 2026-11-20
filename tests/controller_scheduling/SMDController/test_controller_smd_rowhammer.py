"""SMD RowHammer protection: Graphene and PARA aggressor detection and neighbor-row refresh."""

import pytest
import ramulator
import tests.controller_scheduling.harness as cs

pytestmark = pytest.mark.controller_scheduling

LATENCY = 80  # cycles to refresh one row
BLAST_RADIUS = 1
BUSY = LATENCY * 2 * BLAST_RADIUS

def make_dram():
    return ramulator.dram.DDR4_SMD(
        org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1, rows_per_subarray=1024)

REFRESH_WINDOW = 81 * (1 << 16)


def graphene_mechanism(mac=8, **params):
    return ramulator.smd_mechanism.GrapheneProtection(
        **{"mac": mac, "refresh_window": REFRESH_WINDOW, "single_ref_latency": LATENCY, **params}
    )

def para_mechanism(pct, **params):
    return ramulator.smd_mechanism.PARAProtection(
        **{"neighbor_refresh_pct": pct, "single_ref_latency": LATENCY, **params}
    )

def make_dut(mechanisms, row_policy=None):
    """RowHammer protection only: the DRAM does not refresh (NoRefresh); rows close after each access."""
    return cs.ControllerUnderTest.make_smd(
        make_dram(), smd_mechanisms=mechanisms, row_policy=row_policy or ramulator.row_policy.ClosedCAP(cap=1),
    )

def av(dut, *, subarray=0, row=5, bank=0, bankgroup=0):
    return dut.addr_vec(Rank=0, BankGroup=bankgroup, Bank=bank, Subarray=subarray, Row=row, Column=0)

def tick_n(dut, n):
    for _ in range(n):
        dut.tick()

def read(dut, **where):
    dut.send_request("Read", av(dut, **where))
    dut.run_until_idle(max_ticks=4096)

def graphene(mac=8, **params):
    return make_dut([graphene_mechanism(mac, **params)])

def para(pct, **params):
    return make_dut([para_mechanism(pct, **params)])

# -------------------------------------------------------------------
# Graphene
# -------------------------------------------------------------------

def test_graphene_refreshes_neighbors_every_threshold_activations_of_a_row():
    dut = graphene(mac=8)

    for _ in range(8):
        read(dut, row=5)
        read(dut, row=6)
    tick_n(dut, 4 * BUSY)

    stats = dut.stats()
    # rows 5 and 6 each reach the threshold twice
    assert stats["smd_neighbor_refs_issued"] == 4
    assert stats["smd_neighbor_refs_done"] == 4

def test_graphene_stays_quiet_below_the_threshold():
    dut = graphene(mac=8)

    for _ in range(3):
        read(dut, row=5)
        read(dut, row=6)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 0

def test_graphene_counts_banks_separately():
    dut = graphene(mac=8)

    for _ in range(3):
        read(dut, bank=0, row=5)
        read(dut, bank=1, row=5)
    tick_n(dut, 4 * BUSY)

    # three activations in each of two banks: no row reaches 4
    assert dut.stats()["smd_neighbor_refs_issued"] == 0

def test_graphene_counters_start_over_every_refresh_window():
    dut = graphene(mac=8)

    for _ in range(3):
        read(dut, row=5)
    tick_n(dut, REFRESH_WINDOW)
    for _ in range(3):
        read(dut, row=5)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 0

def test_mac_below_two_is_rejected():
    with pytest.raises(Exception, match="mac must be at least 2"):
        graphene(mac=1)

# -------------------------------------------------------------------
# Neighbor refresh locks the aggressor's subarray
# -------------------------------------------------------------------

def test_neighbor_refresh_locks_the_subarray_and_nacks_activations():
    dut = graphene(mac=4)

    read(dut, row=5)
    read(dut, row=6)
    read(dut, row=5)
    dut.send_request("Read", av(dut, row=9))
    history = dut.run_until_idle(max_ticks=8 * BUSY)

    commands = [h.command for h in history]
    assert "ACT_NACK" in commands
    assert dut.stats()["smd_act_nacks"] >= 1
    assert dut.stats()["num_read_reqs_served"] == 4

def test_neighbor_refresh_does_not_lock_other_subarrays():
    dut = graphene(mac=4)

    read(dut, subarray=0, row=5)
    read(dut, subarray=0, row=6)
    read(dut, subarray=0, row=5)
    dut.send_request("Read", av(dut, subarray=1, row=9))
    history = dut.run_until_idle(max_ticks=4 * BUSY)

    assert "ACT_NACK" not in [h.command for h in history]

def test_neighbor_refresh_waits_for_an_open_row_to_close():
    dut = make_dut([graphene_mechanism(mac=4)], row_policy=ramulator.row_policy.Open())
    read(dut, row=5)
    read(dut, row=6)
    read(dut, row=5)
    tick_n(dut, 2 * BUSY)

    stats = dut.stats()
    assert stats["smd_neighbor_refs_issued"] == 1
    assert stats["smd_neighbor_refs_done"] == 0

# -------------------------------------------------------------------
# PARA
# -------------------------------------------------------------------

def test_para_with_certain_probability_refreshes_after_every_activation():
    dut = para(100.0)

    for row in range(6):
        read(dut, row=10 + 3 * row)
        tick_n(dut, 2 * BUSY)

    stats = dut.stats()
    assert stats["smd_neighbor_refs_issued"] == 6
    assert stats["smd_neighbor_refs_done"] == 6

def test_para_with_zero_probability_never_refreshes():
    dut = para(0.0)

    for row in range(6):
        read(dut, row=10 + 3 * row)
    tick_n(dut, 2 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 0

def test_para_partial_nacks_arise_from_chips_drawing_different_numbers():
    dut = para(50.0)

    for bank in range(4):
        read(dut, bank=bank, row=5)
    for bank in range(4):
        read(dut, bank=bank, row=9)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_act_partial_nacks"] > 0

# -------------------------------------------------------------------
# Configuration and statistics
# -------------------------------------------------------------------

def test_disabled_rowhammer_protection_issues_nothing():
    dut = make_dut([])

    for _ in range(10):
        read(dut, row=5)
    tick_n(dut, 4 * BUSY)

    stats = dut.stats()
    assert stats["smd_neighbor_refs_issued"] == 0
    assert stats["smd_act_nacks"] == 0

def test_stats_reset_clears_neighbor_refresh_counters():
    dut = graphene(mac=8)
    for _ in range(4):
        read(dut, row=5)
    tick_n(dut, 4 * BUSY)

    dut.reset_stats()
    stats = dut.stats()
    assert stats["smd_neighbor_refs_issued"] == 0
    assert stats["smd_neighbor_refs_done"] == 0

def test_rowhammer_protection_composes_with_fixed_rate_refresh():
    dut = make_dut([ ramulator.smd_mechanism.FixedRateRefresh(refresh_period=REFRESH_WINDOW, num_ref_machines=16, single_ref_latency=LATENCY), graphene_mechanism(mac=8),])
    for _ in range(8):
        read(dut, row=5)
        read(dut, row=6)
    tick_n(dut, 4 * BUSY)

    stats = dut.stats()
    assert stats["smd_neighbor_refs_issued"] >= 1
    assert stats["smd_maintenance_ops"] > 0
    assert stats["num_read_reqs_served"] == 16

def test_mechanisms_run_in_the_order_given_and_share_one_lock_table():
    dut = make_dut([
        graphene_mechanism(mac=4),
        ramulator.smd_mechanism.FixedRateRefresh(
            refresh_period=REFRESH_WINDOW, num_ref_machines=16, single_ref_latency=LATENCY),
    ])
    for _ in range(6):
        read(dut, row=5)
        read(dut, row=6)
    tick_n(dut, 6 * BUSY)
    assert dut.stats()["num_read_reqs_served"] == 12

# -------------------------------------------------------------------
# Graphene's replacement rule
# -------------------------------------------------------------------    

def activate_each(dut, rows):
    for row in rows:
        read(dut, row=row)
        tick_n(dut, 100)

# Rows A, B, C = 5, 6, 7 with two counters and threshold 4, A A A B C C C C A:
#   C replaces B (the entry whose count equals the spillover count) and reaches the threshold,
#   A (3 activations) is kept and reaches it on its fourth: two aggressors.
HAMMERING = [5, 5, 5, 6, 7, 7, 7, 7, 5]

def test_graphene_replaces_the_entry_with_the_smallest_count():
    dut = make_dut([graphene_mechanism(mac=8, num_counters=2, legacy_eviction=False)])
    activate_each(dut, HAMMERING)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 2

def test_graphene_llegacy_eviction_reproduces_the_original_simulator():
    dut = make_dut([graphene_mechanism(mac=8, num_counters=2, legacy_eviction=True)])
    activate_each(dut, HAMMERING)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 1

def test_graphene_follows_the_paper_by_default():
    dut = make_dut([graphene_mechanism(mac=8, num_counters=2)])
    activate_each(dut, HAMMERING)
    tick_n(dut, 4 * BUSY)

    assert dut.stats()["smd_neighbor_refs_issued"] == 2
