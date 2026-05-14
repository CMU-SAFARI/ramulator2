import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _collect_issued(dut, *, command, count, max_ticks):
    found = []
    for _ in range(max_ticks):
        for item in dut.tick():
            if item.command == command:
                found.append(item)
                if len(found) == count:
                    return found
    raise AssertionError(f"Did not observe {count} {command} commands in {max_ticks} ticks")


def _level_index(dut, name):
    return dut.level_names.index(name)


def _assert_wildcard_levels(dut, item, names):
    for name in names:
        assert item.addr_vec[_level_index(dut, name)] == dut.ALL


@pytest.mark.parametrize(
    "dram",
    [
        ramulator.dram.HBM3(org_preset="HBM3_8Gb_8hi", timing_preset="HBM3_6400Mbps", nREFI=2),
        ramulator.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps", nREFI=2),
    ],
)
def test_all_bank_refresh_uses_pseudochannel_scope_for_hbm34(dram):
    dut = cs.ControllerUnderTest.make_hbm34(
        dram,
        refresh_manager=ramulator.refresh_manager.AllBank(),
    )

    refs = _collect_issued(dut, command="REFab", count=2, max_ticks=16)
    pc_idx = _level_index(dut, "PseudoChannel")

    assert [item.addr_vec[pc_idx] for item in refs] == [0, 1]
    for item in refs:
        _assert_wildcard_levels(dut, item, ["Sid", "BankGroup", "Bank", "Row", "Column"])


def test_all_bank_refresh_uses_rank_scope_for_ddr4():
    dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", nREFI=4)
    dut = cs.ControllerUnderTest.make_generic_ddr(
        dram,
        refresh_manager=ramulator.refresh_manager.AllBank(),
    )

    ref = _collect_issued(dut, command="REFab", count=1, max_ticks=16)[0]

    assert ref.addr_vec[_level_index(dut, "Rank")] == 0
    _assert_wildcard_levels(dut, ref, ["BankGroup", "Bank", "Row", "Column"])


def test_all_bank_refresh_uses_channel_scope_for_hbm1():
    dram = ramulator.dram.HBM1(org_preset="HBM1_2Gb", timing_preset="HBM1_2Gbps", nREFI=4)
    dut = cs.ControllerUnderTest.make_hbm12(
        dram,
        refresh_manager=ramulator.refresh_manager.AllBank(),
    )

    ref = _collect_issued(dut, command="REFab", count=1, max_ticks=16)[0]

    assert ref.addr_vec[_level_index(dut, "Channel")] == 0
    _assert_wildcard_levels(dut, ref, ["BankGroup", "Bank", "Row", "Column"])
