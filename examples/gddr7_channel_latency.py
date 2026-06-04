"""Compare GDDR7 read latency for 1 channel vs 4 channels under the same load.

In Ramulator one controller == one channel. Under a single-stream workload the
peak *throughput* is frontend-limited (it saturates ~one channel), but spreading
the same requests across 4 channels relieves queueing, so the observable benefit
of 4 channels is *lower latency*.

Run:
    PYTHONPATH=python python3 examples/gddr7_channel_latency.py

Outputs:
    - a table on stdout (CK4 cycles and ns)
    - a bar chart at examples/gddr7_channel_latency.png
"""

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

import ramulator

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

TCK_NS = 0.571  # CK4 period for GDDR7_TEST_28000 (tCK_ps=571)
CHANNEL_COUNTS = [1, 2, 4]


def make_ctrl():
    g7 = ramulator.dram.GDDR7(
        org_preset="GDDR7_16Gb_x8_4ch", timing_preset="GDDR7_TEST_28000", encoding="PAM3"
    )
    return ramulator.controller.GDDR7(
        dram=g7,
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.AllBank(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    )


def run(n_channels):
    frontend = ramulator.frontend.SimpleO3(
        clock_ratio=4,
        traces=["./examples/traces/dummy_inst.trace"],
        num_expected_insts=100000,
        llc_linesize=32,
        translation=ramulator.translation.NoTranslation(max_addr=536870912),
    )
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[make_ctrl() for _ in range(n_channels)],
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
    )
    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    cs = sim.stats["memory_system"]["controller"]
    cs = cs if isinstance(cs, list) else [cs]
    avg_lat = sum(c["avg_read_latency"] for c in cs) / len(cs)
    cycles = max(c["cycles"] for c in cs)
    return avg_lat, cycles


def main():
    results = {n: run(n) for n in CHANNEL_COUNTS}

    print(f"{'channels':>9} {'avg_read_lat (CK4 cyc)':>24} {'(ns)':>8} {'runtime (cyc)':>14}")
    print("-" * 60)
    base = results[1][0]
    for n in CHANNEL_COUNTS:
        lat, cyc = results[n]
        red = 100 * (base - lat) / base
        print(f"{n:>9} {lat:>24.1f} {lat * TCK_NS:>8.1f} {cyc:>14}   ({red:+.1f}% lat)")

    lats_ns = [results[n][0] * TCK_NS for n in CHANNEL_COUNTS]
    fig, ax = plt.subplots(figsize=(6, 4))
    bars = ax.bar([str(n) for n in CHANNEL_COUNTS], lats_ns, color=["#c44", "#ca4", "#4a4"])
    ax.set_xlabel("GDDR7 channels (8-bit each)")
    ax.set_ylabel("avg read latency (ns)")
    ax.set_title("GDDR7 read latency vs channel count\n(same single-stream workload)")
    for b, v in zip(bars, lats_ns):
        ax.text(b.get_x() + b.get_width() / 2, v + 1, f"{v:.1f} ns", ha="center", fontsize=10)
    fig.tight_layout()
    out = Path(__file__).resolve().parent / "gddr7_channel_latency.png"
    fig.savefig(out, dpi=150)
    print(f"\nplot written to: {out}")


if __name__ == "__main__":
    main()
