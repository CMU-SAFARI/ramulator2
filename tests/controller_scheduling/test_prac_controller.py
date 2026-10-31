import pytest

import ramulator
import tests.controller_scheduling.harness as cs


pytestmark = pytest.mark.controller_scheduling


def _make_prac_dut(**dram_overrides):
    dram = ramulator.dram.DDR5(
        org_preset="DDR5_16Gb_x8", timing_preset="DDR5_6400AN", **dram_overrides
    )
    controller = ramulator.controller.PRAC(
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        dram=dram,
        controller_plugins=[],
    )
    return cs.ControllerUnderTest(controller)


def test_prac_controller_warns_on_a_dram_without_prac(capfd):
    # PRAC's alert protocol on non-PRAC core timings is allowed for experiments, with a warning.
    dut = _make_prac_dut()
    assert 'prac="on"' in capfd.readouterr().err

    dut.send_request("Read", dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0))
    dut.assert_commands(["ACT", "RD"], history=dut.run_until_idle(max_ticks=256))


def test_prac_controller_runs_on_a_prac_dram():
    dut = _make_prac_dut(prac="on")
    dut.send_request("Read", dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0))

    history = dut.run_until_idle(max_ticks=256)

    dut.assert_commands(["ACT", "RD"], history=history)
