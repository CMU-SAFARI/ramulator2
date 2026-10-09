"""The Ayna plugin against Ayna's own runners, on Ramulator's speedbins and organizations.

Each case runs a workload through the controller with the plugin and a CmdTraceRecorder, then
replays the recorded commands through Ayna's unmodified HBM2_runner / HBM3_runner with configs
exported from the same DRAM object. Energies must match per pseudo-channel to the 0.001 pJ the
runners print. Needs Ayna's runners (see tests/power/ayna_reference.py); skipped otherwise.
"""

import pytest

import ramulator
import ramulator.power
from tests.power.ayna_reference import Workload, run_ayna, run_workload

pytestmark = pytest.mark.power

DEVICES = [
    ("HBM2", "HBM2_4Gb", "HBM2_1600Mbps"),  # 16 banks per pseudo-channel
    ("HBM2", "HBM2_8Gb", "HBM2_2000Mbps"),  # 2 SIDs: 32 banks
    ("HBM2", "HBM2_2Gb", "HBM2_2400Mbps"),  # 16 banks; tCK_ps is rounded (833 ps)
    ("HBM3", "HBM3_16Gb_4hi", "HBM3_6400Mbps"),  # 16 banks
    ("HBM3", "HBM3_16Gb_8hi", "HBM3_6400Mbps"),  # 32 banks
    ("HBM3", "HBM3_32Gb_16hi", "HBM3_6400Mbps"),  # 64 banks
    ("HBM4", "HBM4_32Gb_4Hi", "HBM4_8000Mbps"),
    ("HBM4", "HBM4_32Gb_8Hi", "HBM4_8000Mbps"),
    ("HBM4", "HBM4_32Gb_8Hi", "HBM4_8000Mbps_folded_banks_baseline"),
]

WORKLOADS = [
    Workload("open_page", requests=3000, tail_ticks=4000, seed=1),
    Workload("closed_page", requests=3000, tail_ticks=4000, closed_page=True, seed=2),
    Workload("refresh_heavy", requests=400, request_interval=150, tail_ticks=40000, seed=3),
]


def _check(dram, workload, tmp_path, ayna_bin, **ayna_kwargs):
    run = run_workload(dram, workload, str(tmp_path), **ayna_kwargs)
    ayna = run_ayna(ayna_bin, dram, run, str(tmp_path))
    domains = run.stats["domain_E_total"]
    assert sorted(ayna) == list(range(len(domains)))
    for pc, energy in enumerate(domains):
        _, reference = ayna[pc]
        assert energy == pytest.approx(reference, abs=5e-4, rel=1e-12), f"pseudo-channel {pc}"
    assert run.stats["E_total"] == pytest.approx(
        sum(e for _, e in ayna.values()), abs=5e-4 * len(domains)
    )
    return run


@pytest.mark.parametrize("workload", WORKLOADS, ids=lambda w: w.name)
@pytest.mark.parametrize("device", DEVICES, ids=lambda d: f"{d[1]}-{d[2]}")
def test_matches_ayna(device, workload, tmp_path, ayna_bin):
    std, org, timing = device
    dram = getattr(ramulator.dram, std)(org_preset=org, timing_preset=timing)
    run = _check(dram, workload, tmp_path, ayna_bin)
    # The workloads exercise what they are named for.
    if workload.closed_page:
        assert run.commands["RDA"] > 0 and run.commands["WRA"] > 0
    assert run.commands["REFab"] > 0 and run.commands["PREab"] > 0
    assert run.stats["unmodelled_commands"] == 0


@pytest.mark.parametrize(
    "device, workload, ayna_kwargs",
    [
        (("HBM2", "HBM2_8Gb", "HBM2_2000Mbps"), WORKLOADS[0], {"variation": True}),
        (
            ("HBM3", "HBM3_16Gb_8hi", "HBM3_6400Mbps"),
            WORKLOADS[0],
            {"data_pattern": ramulator.power.DataPattern.static()},
        ),
        (
            ("HBM4", "HBM4_32Gb_8Hi", "HBM4_8000Mbps"),
            WORKLOADS[1],
            {"data_pattern": ramulator.power.DataPattern(dq_rate=1.0, tsv_rate=0.25, bg_rate=0.0)},
        ),
    ],
    ids=["hbm2-variation", "hbm3-static-data", "hbm4-data-pattern"],
)
def test_matches_ayna_with_electrical_options(device, workload, ayna_kwargs, tmp_path, ayna_bin):
    std, org, timing = device
    dram = getattr(ramulator.dram, std)(org_preset=org, timing_preset=timing)
    _check(dram, workload, tmp_path, ayna_bin, **ayna_kwargs)


def test_matches_ayna_after_warmup(tmp_path, ayna_bin):
    dram = ramulator.dram.HBM3(org_preset="HBM3_16Gb_8hi", timing_preset="HBM3_6400Mbps")
    workload = Workload("warmup", requests=3000, tail_ticks=20000, warmup_ticks=5000, seed=4)
    run = _check(dram, workload, tmp_path, ayna_bin)
    assert run.window_start == 5000
