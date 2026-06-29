#!/usr/bin/env python3
"""Plot gem5 PyTrafficGen latency-throughput results from one CSV."""

import argparse
import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "python"))

from tests.latency_throughput.utils.plot import _CURVE_CMAP, _CURVE_NORM


def parse_args():
    parser = argparse.ArgumentParser(description="Plot gem5 PyTrafficGen CSV output.")
    parser.add_argument("--csv", required=True, type=Path)
    parser.add_argument("--out-dir", type=Path)
    parser.add_argument("--prefix")
    return parser.parse_args()


def load_rows(csv_path):
    rows = []
    with csv_path.open() as fh:
        for row in csv.DictReader(fh):
            row["channels"] = int(row["channels"])
            row["read_ratio"] = int(row["read_ratio"])
            row["intensity"] = float(row["intensity"])
            row["observed_gbps"] = float(row["observed_gbps"])
            row["avg_read_latency_ns"] = float(row["avg_read_latency_ns"])
            rows.append(row)
    if not rows:
        raise ValueError(f"CSV has no data rows: {csv_path}")
    channels = {row["channels"] for row in rows}
    if len(channels) != 1:
        raise ValueError(
            "plot.py expects exactly one channel count per CSV; "
            f"got {sorted(channels)}"
        )
    return rows


def traffic_order(rows):
    present = {row["traffic"] for row in rows}
    ordered = [traffic for traffic in ("stream", "random") if traffic in present]
    extras = sorted(present - set(ordered))
    return ordered + extras


def output_prefix(rows):
    row = rows[0]
    return f"{row['dram']}_{row['addr_mapper']}_ch{row['channels']}_lat_tp"


def speedbin_label(row):
    timing = row["timing_preset"]
    prefix = f"{row['dram']}_"
    if timing.startswith(prefix):
        return timing[len(prefix):]
    return timing.replace("_", "-")


def queue_size_label(row):
    read_q = row.get("read_buffer_size") or "32"
    write_q = row.get("write_buffer_size") or "32"
    if read_q == write_q:
        return read_q
    return f"{read_q}/{write_q}"


def figure_title(rows):
    row = rows[0]
    return (
        f"{row['dram']}-{speedbin_label(row)}, {row['channels']}x Channel\n"
        f"{row['addr_mapper']}, q={queue_size_label(row)}"
    )


def plot_rows(rows, out_dir, prefix):
    traffics = traffic_order(rows)
    fig_width = 13 if len(traffics) > 1 else 7
    fig, axes = plt.subplots(
        1,
        len(traffics),
        figsize=(fig_width, 5),
        sharex=True,
        sharey=True,
    )
    if len(traffics) == 1:
        axes = [axes]
    fig.patch.set_facecolor("white")

    for ax, traffic in zip(axes, traffics):
        ax.set_facecolor("white")
        traffic_rows = [row for row in rows if row["traffic"] == traffic]
        for read_ratio in sorted({row["read_ratio"] for row in traffic_rows}):
            curve = sorted(
                (row for row in traffic_rows if row["read_ratio"] == read_ratio),
                key=lambda row: row["intensity"],
            )
            ax.plot(
                [row["observed_gbps"] for row in curve],
                [row["avg_read_latency_ns"] for row in curve],
                "o-",
                color=_CURVE_CMAP(_CURVE_NORM(read_ratio)),
                linewidth=2,
                markersize=5,
                zorder=3,
            )

        ax.text(
            0.5,
            0.97,
            traffic.capitalize(),
            transform=ax.transAxes,
            ha="center",
            va="top",
            fontsize=13,
            fontweight="bold",
            bbox={
                "boxstyle": "round,pad=0.35",
                "facecolor": "white",
                "edgecolor": "#888",
                "linewidth": 1.0,
            },
            zorder=4,
        )
        ax.grid(True, linestyle="--", linewidth=0.8, alpha=0.4)
        ax.tick_params(
            axis="both",
            which="major",
            direction="in",
            length=5,
            width=1.2,
            labelsize=12,
        )
        for spine in ax.spines.values():
            spine.set_linewidth(1.5)

    x_max = max(row["observed_gbps"] for row in rows)
    y_max = max(row["avg_read_latency_ns"] for row in rows)
    for ax in axes:
        ax.set_xlim(left=0, right=x_max * 1.08 if x_max > 0 else 1)
        ax.set_ylim(bottom=0, top=y_max * 1.08 if y_max > 0 else 1)
        ax.set_xlabel("DRAM Throughput (GB/s)", fontsize=15, labelpad=8)
    axes[0].set_ylabel("DRAM Access Latency (ns)", fontsize=15, labelpad=8)

    plot_bottom = 0.15
    plot_top = 0.82
    fig.suptitle(
        figure_title(rows),
        fontsize=18,
        fontweight="bold",
        y=0.98,
        linespacing=0.95,
    )

    sm = plt.cm.ScalarMappable(cmap=_CURVE_CMAP, norm=_CURVE_NORM)
    sm.set_array([])
    fig.subplots_adjust(
        wspace=0.08,
        right=0.84,
        top=plot_top,
        bottom=plot_bottom,
    )
    cax = fig.add_axes([0.87, plot_bottom, 0.018, plot_top - plot_bottom])
    cbar = fig.colorbar(sm, cax=cax)
    cbar.set_label("% Reads", fontsize=13)
    cbar.ax.tick_params(labelsize=11)

    out_dir.mkdir(parents=True, exist_ok=True)
    png = out_dir / f"{prefix}.png"
    pdf = out_dir / f"{prefix}.pdf"
    fig.savefig(png, dpi=300, bbox_inches="tight")
    fig.savefig(pdf, bbox_inches="tight")
    plt.close(fig)
    return png, pdf


def main():
    args = parse_args()
    rows = load_rows(args.csv)
    out_dir = args.out_dir.resolve() if args.out_dir else args.csv.resolve().parent
    prefix = args.prefix or output_prefix(rows)
    png, pdf = plot_rows(rows, out_dir, prefix)
    print(f"Wrote plot PNG: {png}")
    print(f"Wrote plot PDF: {pdf}")


if __name__ == "__main__":
    main()
