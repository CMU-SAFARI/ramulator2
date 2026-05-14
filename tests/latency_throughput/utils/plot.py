"""Latency-throughput plot generation.

Two entry points:
- `plot_curves`: just the lat-tp curves (one per read ratio).
- `plot_curves_annotated`: curves plus reference lines for theoretical
  peak BW, streaming-only BW, and ideal unloaded latency, plus a summary
  text panel.

Both produce a PNG at 300 DPI.
"""

import os
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")  # non-interactive backend
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap

from tests.latency_throughput.utils.spec import Spec

_CURVE_CMAP = LinearSegmentedColormap.from_list(
    "custom_blue_red", [(0.0, "#0000AA"), (1.0, "#AA0000")]
)
_CURVE_NORM = mcolors.Normalize(vmin=50, vmax=100)


def extract_curves(raw_results: dict, spec: Spec) -> dict:
    """Convert raw sweep results into per-read-ratio lat-tp curves.

    Returns: {
        read_ratio: {
            "bw":   [float, ...],   # GB/s, sorted low-BW first
            "lat":  [float, ...],   # ns
            "nops": [int, ...],     # corresponding NOP values
        }
    }
    """
    by_rr = defaultdict(list)
    for (nop, rr), stats in raw_results.items():
        by_rr[rr].append((nop, stats))

    curves = {}
    for rr, entries in sorted(by_rr.items()):
        # Sort by NOP descending so the lowest-BW point comes first.
        entries.sort(key=lambda x: -x[0])
        bw_list, lat_list, nop_list = [], [], []
        for nop, stats in entries:
            lat_cycles = stats["frontend"]["avg_probe_latency"]

            bw = stats["memory_system"]["controller"]["total_throughput_MBps"] / 1000.0
            lat = lat_cycles * spec.time_unit_ns

            bw_list.append(bw)
            lat_list.append(lat)
            nop_list.append(nop)

        curves[rr] = {"bw": bw_list, "lat": lat_list, "nops": nop_list}

    return curves


def _draw_base_plot(curves, cfg: dict):
    """Build the figure with the lat-tp curves and basic axis styling."""
    fig, ax = plt.subplots(figsize=(10, 6))
    fig.patch.set_facecolor("white")
    ax.set_facecolor("white")

    for rr in sorted(curves.keys()):
        c = curves[rr]
        ax.plot(
            c["bw"], c["lat"],
            "o-",
            color=_CURVE_CMAP(_CURVE_NORM(rr)),
            linewidth=2,
            markersize=5,
            zorder=3,
        )

    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)
    ax.set_title(cfg["timing_preset"].replace("_", "-"), fontsize=20, fontweight="bold", pad=16)
    ax.set_xlabel("DRAM Throughput (GB/s)", fontsize=16, labelpad=8)
    ax.set_ylabel("Random Probe Access Latency (ns)", fontsize=16, labelpad=8)
    ax.grid(True, linestyle="--", linewidth=0.8, alpha=0.4)
    ax.tick_params(axis="both", which="major", direction="in", length=5, width=1.2, labelsize=13)
    for spine in ax.spines.values():
        spine.set_linewidth(1.5)

    sm = plt.cm.ScalarMappable(cmap=_CURVE_CMAP, norm=_CURVE_NORM)
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=ax, pad=0.02)
    cbar.set_label("% Reads", fontsize=14)
    cbar.ax.tick_params(labelsize=12)

    return fig, ax


def _save(fig, output_dir: str, std_name: str) -> str:
    os.makedirs(output_dir, exist_ok=True)
    png_path = os.path.join(output_dir, f"{std_name}_lat_tp.png")
    plt.savefig(png_path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return png_path


def plot_curves(curves: dict, cfg: dict, output_dir: str) -> str:
    """Plot just the lat-tp curves with no reference annotations."""
    fig, _ = _draw_base_plot(curves, cfg)
    return _save(fig, output_dir, cfg["name"])


def plot_curves_annotated(
    curves: dict,
    cfg: dict,
    *,
    lat_result: dict,
    bw_result: dict,
    streaming_result: dict | None,
    output_dir: str,
) -> str:
    """Plot lat-tp curves with reference lines and a summary text panel.

    `bw_result` may be either a `check_peak_bandwidth` result (no refresh)
    or a `check_refresh_limited_bandwidth` result; the latter has a
    `max_achievable_bw` key, which takes precedence as the peak reference.
    `streaming_result` is optional — if provided, a streaming reference
    line is drawn.
    """
    fig, ax = _draw_base_plot(curves, cfg)
    x_refs = [bw_result["max_theoretical_bw"]]

    ax.axvline(
        bw_result["max_theoretical_bw"],
        color="#cc7722",
        linestyle="--",
        linewidth=1.5,
        alpha=0.7,
        label="Max. Theoretical Throughput",
    )
    if "max_achievable_bw" in bw_result:
        x_refs.append(bw_result["max_achievable_bw"])
        ax.axvline(
            bw_result["max_achievable_bw"],
            color="#228833",
            linestyle="--",
            linewidth=1.5,
            alpha=0.7,
            label="Refresh-Adjusted Achievable Throughput",
        )
    if streaming_result is not None:
        x_refs.append(streaming_result["measured_streaming_bw"])
        ax.axvline(
            streaming_result["measured_streaming_bw"],
            color="#4477AA", linestyle="-.", linewidth=1.5, alpha=0.7,
            label="Streaming-Only Throughput (Measured)",
        )
    ax.axhline(
        lat_result["expected_ns"],
        color="#AA3377", linestyle=":", linewidth=1.5, alpha=0.7,
        label="Unloaded Latency (Ideal)",
    )
    curve_max_bw = max(max(c["bw"]) for c in curves.values())
    ax.set_xlim(left=0, right=max(curve_max_bw, *x_refs) * 1.08)

    legend = ax.legend(
        fontsize=10, frameon=True, edgecolor="#666",
        facecolor="white", framealpha=0.9, loc="upper left",
    )
    legend.get_frame().set_linewidth(1.5)

    left_lines = [
        f"Unloaded Latency (Ideal): {lat_result['expected_ns']:.1f} ns",
        f"Unloaded Latency (Measured): {lat_result['measured_ns']:.1f} ns",
    ]
    right_lines = [
        f"Max. Theoretical Throughput: {bw_result['max_theoretical_bw']:.1f} GB/s",
    ]
    if "max_achievable_bw" in bw_result:
        right_lines.append(
            f"Refresh-Adjusted Achievable Throughput: {bw_result['max_achievable_bw']:.1f} GB/s"
        )
    if streaming_result is not None:
        right_lines.append(
            f"Streaming-Only Throughput (Measured): "
            f"{streaming_result['measured_streaming_bw']:.1f} GB/s"
        )

    fig.subplots_adjust(bottom=0.30, top=0.92)
    pos = ax.get_position()
    cx = (pos.x0 + pos.x1) / 2
    text_bbox = dict(boxstyle="round,pad=0.4", fc="white", ec="#ccc", linewidth=0.8)
    fig.text(cx, 0.15, "\n".join(left_lines), ha="center", va="center",
             fontsize=9.5, color="#555", family="monospace", bbox=text_bbox)
    fig.text(cx, 0.04, "\n".join(right_lines), ha="center", va="center",
             fontsize=9.5, color="#555", family="monospace", bbox=text_bbox)

    return _save(fig, output_dir, cfg["name"])
