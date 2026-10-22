"""PUMTrace frontend driving DDR4_PUM through the PUM controller."""

import pytest

import ramulator

pytestmark = pytest.mark.smoke


def run(tmp_path, lines, channels=1):
    dram = ramulator.dram.DDR4_PUM(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R",
                                   rank=1, rows_per_subarray=1024)
    trace = tmp_path / "kernel.trace"
    trace.write_text("\n".join(lines) + "\n")
    org, _ = dram.resolve()
    frontend = ramulator.frontend.PUMTrace(
        clock_ratio=1,
        path=str(trace),
        request_types=list(type(dram).supported_requests),
        level_counts=[org.get(level.lower(), 1) for level in type(dram).levels],
    )
    controllers = [
        ramulator.controller.PUM(
            dram=dram,
            scheduler=ramulator.scheduler.FRFCFS(),
            refresh_manager=ramulator.refresh_manager.AllBank(),
            row_policy=ramulator.row_policy.Open(),
            addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        )
        for _ in range(channels)
    ]
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=controllers,
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )
    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats, dram.resolve()[1]


# addr_vec: Channel, Rank, BankGroup, Bank, Subarray, Row, Column
def test_fence_waits_for_outstanding_requests(tmp_path):
    parallel, t = run(tmp_path, ["PUM_AAP 0,0,0,0,0,0,0", "PUM_AAP 0,0,0,1,0,0,0"])
    fenced, _ = run(tmp_path, ["PUM_AAP 0,0,0,0,0,0,0", "FENCE", "PUM_AAP 0,0,0,1,0,0,0"])
    latency = t["nRAS"] + t["nRP"] + (t["nRP"] + 3) // 4

    assert parallel["frontend"]["cycles"] < latency + 10
    assert fenced["frontend"]["cycles"] >= 2 * latency


def test_mixed_trace_reaches_dram(tmp_path):
    stats, _ = run(tmp_path, [
        "# reads and writes to distinct locations, then compute",
        "W 0,0,0,0,0,3,1",
        "W 0,0,0,1,0,3,1",
        "R 0,0,0,2,0,3,1",
        "PUM_PMAJ3 0,0,1,0,2,0,0  # bank group 1",
        "PUM_AAP 0,0,0,0,1,0,0",
    ])
    frontend = stats["frontend"]
    for name, count in (("Write", 2), ("Read", 1), ("PUM_PMAJ3", 1), ("PUM_AAP", 1)):
        assert frontend[f"num_{name}_sent"] == count
        assert frontend[f"num_{name}_completed"] == count

    ctrl = stats["memory_system"]["controller"]
    assert ctrl["num_read_reqs_forwarded"] == 0
    assert ctrl["num_write_reqs_coalesced"] == 0
    assert ctrl["num_write_reqs_served"] == 2
    assert ctrl["num_pum_reqs_served"] == 2


def test_compute_goes_to_the_named_channel(tmp_path):
    stats, _ = run(tmp_path, ["PUM_AAP 1,0,0,0,0,0,0", "PUM_AAP 1,0,0,1,0,0,0", "PUM_AP 0,0,0,0,0,0,0"],
                   channels=2)
    ch0, ch1 = stats["memory_system"]["controller"]
    assert ch0["num_pum_AP_reqs_served"] == 1
    assert ch0["num_pum_AAP_reqs_served"] == 0
    assert ch1["num_pum_AAP_reqs_served"] == 2
