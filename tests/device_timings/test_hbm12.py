import pytest

import ramulator
import tests.device_timings.harness as device_timings


pytestmark = pytest.mark.device_timings


PRESETS = [
    ("HBM1", "HBM1_4Gb", "HBM1_2Gbps"),
    ("HBM2", "HBM2_2Gb", "HBM2_2000Mbps"),
    ("HBM2", "HBM2_8Gb", "HBM2_2000Mbps"),
]


def make_dut(standard, org_preset, timing_preset):
    dram = getattr(ramulator.dram, standard)(org_preset=org_preset, timing_preset=timing_preset)
    return device_timings.DeviceUnderTest(dram)


@pytest.mark.parametrize(("standard", "org_preset", "timing_preset"), PRESETS)
def test_hbm12_refpb_to_the_same_bank_waits_nrfcpb(standard, org_preset, timing_preset):
    # JESD235D §6.3.2.6 (PDF p.50) and Table 35 Note 3 (PDF p.51): a bank is refreshed again
    # only in the next set, which starts at least tRFCSB after the current one.
    dut = make_dut(standard, org_preset, timing_preset)
    bank = dut.addr_vec(BankGroup=1, Bank=2)
    dut.issue("REFpb", bank, clk=0)
    dut.assert_earliest_ready_at("REFpb", bank, dut.timings["nRFCpb"])


@pytest.mark.parametrize(("standard", "org_preset", "timing_preset"), PRESETS)
def test_hbm12_refpb_to_another_bank_waits_only_nrrefd(standard, org_preset, timing_preset):
    # JESD235D Table 35 (PDF p.51): SINGLE BANK REFRESH to a different bank waits tRREFD.
    dut = make_dut(standard, org_preset, timing_preset)
    dut.issue("REFpb", dut.addr_vec(BankGroup=1, Bank=2), clk=0)
    for other in (dut.addr_vec(BankGroup=1, Bank=3), dut.addr_vec(BankGroup=2, Bank=2)):
        dut.assert_earliest_ready_at("REFpb", other, dut.timings["nRREFD"])
