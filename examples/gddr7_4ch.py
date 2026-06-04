"""Example GDDR7 configuration with all 4 channels (each 8-bit wide).

In Ramulator, one controller == one channel; the DRAM org preset describes a
single 8-bit GDDR7 channel. The full 4-channel device is built at the
*system* level by instantiating four GDDR7 controllers and letting the channel
mapper interleave requests across them. This is the standard Ramulator
multi-channel pattern (no GDDR7-specific code required).

Compare with examples/gddr7.py, which runs a single channel.
"""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import ramulator

NUM_CHANNELS = 4


def make_gddr7_controller(encoding="PAM3"):
    dram = ramulator.dram.GDDR7(
        org_preset="GDDR7_16Gb_x8_4ch",
        timing_preset="GDDR7_TEST_28000",
        encoding=encoding,  # "PAM3" -> BL16/nBL=2, "NRZ" -> BL32/nBL=4
    )
    return ramulator.controller.GDDR7(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.AllBank(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    )


frontend = ramulator.frontend.SimpleO3(
    clock_ratio=4,
    traces=["./examples/traces/dummy_inst.trace"],
    num_expected_insts=100000,
    llc_linesize=32,
    translation=ramulator.translation.NoTranslation(max_addr=536870912),
)

controllers = [make_gddr7_controller() for _ in range(NUM_CHANNELS)]

mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=1,
    controllers=controllers,
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
sim.run()

if sim.stats:
    ctrl_stats = sim.stats["memory_system"]["controller"]
    # With >1 controller, this is a list (one entry per channel).
    per_ch = ctrl_stats if isinstance(ctrl_stats, list) else [ctrl_stats]
    total_reads = 0
    print(f"GDDR7 device: {len(per_ch)} channels (8-bit each)")
    for i, c in enumerate(per_ch):
        total_reads += c["num_read_reqs"]
        print(
            f"  ch{i}: reads={c['num_read_reqs']:6d} "
            f"writes={c['num_write_reqs']:6d} "
            f"avg_read_latency={c['avg_read_latency']:.1f} cyc "
            f"cycles={c['cycles']}"
        )
    print(f"  total reads across channels: {total_reads}")
