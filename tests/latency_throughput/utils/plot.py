"""Latency-throughput plot generation for validation tests.

Produces one plot per DRAM standard (PNG at 300 DPI) with:
- Lat-TP curves colored by read ratio (custom blue-red colormap)
- Reference lines for theoretical throughput and unloaded latency
- Summary below the plot with key measured vs expected values
"""

import os

import matplotlib

matplotlib.use("Agg")  # non-interactive backend
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap


def plot_lat_tp(
    curves,
    std_name,
    check_results,
    output_dir="tests/latency_throughput/plots/fast",
    verbose=False,
):
    """Generate and save a latency-throughput plot for one DRAM standard.

    Args:
        curves: {read_ratio: {"bw": [...], "lat": [...], "nops": [...]}}
        std_name: e.g. "DDR4"
        check_results: {"latency": lat_result_dict, "bandwidth": bw_result_dict}
        output_dir: directory to save the plot

    Returns:
        Path to the saved PNG plot file.
    """
    os.makedirs(output_dir, exist_ok=True)

    lat_res = check_results["latency"]
    bw_res = check_results["bandwidth"]

    read_ratios = sorted(curves.keys())
    cmap = LinearSegmentedColormap.from_list(
        "custom_blue_red", [(0.0, "#0000AA"), (1.0, "#AA0000")]
    )
    norm = mcolors.Normalize(vmin=50, vmax=100)

    fig, ax = plt.subplots(figsize=(10, 6))
    fig.patch.set_facecolor("white")
    ax.set_facecolor("white")

    # Data curves (heaviest visual element)
    for rr in read_ratios:
        c = curves[rr]
        color = cmap(norm(rr))
        ax.plot(c["bw"], c["lat"], "o-", color=color, linewidth=2, markersize=5, zorder=3)

    # Reference lines (only in verbose mode)
    if verbose:
        peak_bw = bw_res["max_theoretical_bw"]
        ax.axvline(
            peak_bw,
            color="#cc7722",
            linestyle="--",
            linewidth=1.5,
            alpha=0.7,
            label="Max. Theoretical Throughput",
        )

        streaming_res = check_results.get("streaming")
        if streaming_res:
            streaming_bw = streaming_res["measured_streaming_bw"]
            ax.axvline(
                streaming_bw,
                color="#4477AA",
                linestyle="-.",
                linewidth=1.5,
                alpha=0.7,
                label="Streaming-Only Throughput (Measured)",
            )

        expected_lat = lat_res["expected_ns"]
        ax.axhline(
            expected_lat,
            color="#AA3377",
            linestyle=":",
            linewidth=1.5,
            alpha=0.7,
            label="Unloaded Latency (Ideal)",
        )

    # Axis limits and labels
    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)

    from tests.latency_throughput.testcases import STANDARDS

    cfg = STANDARDS[std_name]
    ax.set_title(cfg["timing_preset"].replace("_", "-"), fontsize=20, fontweight="bold", pad=16)
    ax.set_xlabel("Throughput (GB/s)", fontsize=16, labelpad=8)
    ax.set_ylabel("Random Probe Access Latency (ns)", fontsize=16, labelpad=8)

    # Grid (light, behind data)
    ax.grid(True, linestyle="--", linewidth=0.8, alpha=0.4)

    # Ticks
    ax.tick_params(axis="both", which="major", direction="in", length=5, width=1.2, labelsize=13)

    # Spines
    for spine in ax.spines.values():
        spine.set_linewidth(1.5)

    # Legend (only in verbose mode — reference lines provide the labels)
    if verbose:
        legend = ax.legend(
            fontsize=10,
            frameon=True,
            edgecolor="#666",
            facecolor="white",
            framealpha=0.9,
            loc="upper left",
        )
        legend.get_frame().set_linewidth(1.5)

    # Colorbar
    sm = plt.cm.ScalarMappable(cmap=cmap, norm=norm)
    sm.set_array([])
    cbar = fig.colorbar(sm, ax=ax, pad=0.02)
    cbar.set_label("% Reads", fontsize=14)
    cbar.ax.tick_params(labelsize=12)

    # Summary below the plot (only in verbose mode)
    if verbose:
        peak_bw = bw_res["max_theoretical_bw"]
        streaming_res = check_results.get("streaming")

        left_lines = [
            f"Unloaded Latency (Ideal): {lat_res['expected_ns']:.1f} ns",
            f"Unloaded Latency (Measured): {lat_res['measured_ns']:.1f} ns",
        ]
        right_lines = [
            f"Max. Theoretical Throughput: {peak_bw:.1f} GB/s",
        ]
        if streaming_res:
            streaming_bw = streaming_res["measured_streaming_bw"]
            right_lines.append(
                f"Streaming-Only Throughput (Measured): {streaming_bw:.1f} GB/s"
            )

        fig.subplots_adjust(bottom=0.30, top=0.92)
        pos = ax.get_position()
        cx = (pos.x0 + pos.x1) / 2
        text_bbox = dict(boxstyle="round,pad=0.4", fc="white", ec="#ccc", linewidth=0.8)
        fig.text(
            cx,
            0.15,
            "\n".join(left_lines),
            ha="center",
            va="center",
            fontsize=9.5,
            color="#555",
            family="monospace",
            bbox=text_bbox,
        )
        fig.text(
            cx,
            0.04,
            "\n".join(right_lines),
            ha="center",
            va="center",
            fontsize=9.5,
            color="#555",
            family="monospace",
            bbox=text_bbox,
        )

    png_path = os.path.join(output_dir, f"{std_name}_lat_tp.png")
    plt.savefig(png_path, dpi=300, bbox_inches="tight")
    plt.close(fig)
    return png_path
