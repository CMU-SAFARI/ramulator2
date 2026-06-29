#!/usr/bin/env python3
"""Run gem5 PyTrafficGen latency-throughput sweep and write result into CSV."""

import argparse
import csv
import json
import math
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "python"))

import ramulator
from ramulator.dram.spec import DRAMStandard


CONFIG_PATH = Path(__file__).resolve().with_name("config.py")
DEFAULT_READ_RATIOS = [100, 90, 80, 70, 60, 50]
DEFAULT_INTENSITIES = [i / 100.0 for i in range(5, 101, 5)]
ADDR_MAPPER_ALIASES = {
    "MOP4CLXOR": "MOP4CLXOR",
    "RoBaRaCoCh": "RoBaRaCoCh",
    "ChRaBaRoCo": "ChRaBaRoCo",
}
TRAFFIC_ALIASES = {
    "stream": "linear",
    "random": "random",
}
DRAM_PROFILES = {
    "DDR3": {
        "dram_class": "DDR3",
        "org_preset": "DDR3_2Gb_x8",
        "timing_preset": "DDR3_1600H",
        "controller_class": "GenericDDR",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "DDR4": {
        "dram_class": "DDR4",
        "org_preset": "DDR4_8Gb_x8",
        "timing_preset": "DDR4_2400R",
        "controller_class": "GenericDDR",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "DDR5": {
        "dram_class": "DDR5",
        "org_preset": "DDR5_16Gb_x8",
        "timing_preset": "DDR5_4800AN",
        "controller_class": "GenericDDR",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "GDDR6": {
        "dram_class": "GDDR6",
        "org_preset": "GDDR6_8Gb_x16",
        "timing_preset": "GDDR6_14000_1250mV_double",
        "controller_class": "GenericDDR",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "GDDR7": {
        "dram_class": "GDDR7",
        "org_preset": "GDDR7_16Gb_x8",
        "timing_preset": "GDDR7_28000_PAM3",
        "controller_class": "GDDR7",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "HBM1": {
        "dram_class": "HBM1",
        "org_preset": "HBM1_2Gb",
        "timing_preset": "HBM1_2Gbps",
        "controller_class": "HBM12",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "HBM2": {
        "dram_class": "HBM2",
        "org_preset": "HBM2_2Gb",
        "timing_preset": "HBM2_2000Mbps",
        "controller_class": "HBM12",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "HBM3": {
        "dram_class": "HBM3",
        "org_preset": "HBM3_8Gb_8hi",
        "timing_preset": "HBM3_6400Mbps",
        "controller_class": "HBM34",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "HBM4": {
        "dram_class": "HBM4",
        "org_preset": "HBM4_32Gb_8Hi",
        "timing_preset": "HBM4_8000Mbps",
        "controller_class": "HBM34",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "LPDDR5": {
        "dram_class": "LPDDR5",
        "org_preset": "LPDDR5_8Gb_x16",
        "timing_preset": "LPDDR5_6400",
        "controller_class": "LPDDR5",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
    "LPDDR6": {
        "dram_class": "LPDDR6",
        "org_preset": "LPDDR6_16Gb_x12",
        "timing_preset": "LPDDR6_10667_BL24",
        "controller_class": "LPDDR6",
        "scheduler_class": "FRFCFSRowHit",
        "row_policy": "Open",
    },
}
STAT_FLOAT_RE = re.compile(r"^([A-Za-z0-9_:.]+)\s+([-+A-Za-z0-9_.]+)")


@dataclass(frozen=True)
class ProfileSpec:
    bytes_per_req: int
    peak_gbps_per_channel: float


CSV_FIELDS = [
    "dram",
    "dram_class",
    "org_preset",
    "timing_preset",
    "controller_class",
    "scheduler_class",
    "row_policy",
    "channels",
    "traffic",
    "read_ratio",
    "intensity",
    "rate_bps",
    "peak_gbps",
    "block_size",
    "duration",
    "memory_size",
    "addr_mapper",
    "refresh_manager",
    "channel_mapper",
    "clock_ratio",
    "read_buffer_size",
    "write_buffer_size",
    "observed_gbps",
    "read_bw_Bps",
    "write_bw_Bps",
    "avg_read_latency_ns",
    "avg_write_latency_ns",
    "total_reads",
    "total_writes",
    "run_dir",
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run one-channel-count gem5 PyTrafficGen sweeps."
    )
    parser.add_argument("--dram", required=True, choices=sorted(DRAM_PROFILES))
    parser.add_argument("--channels", type=int, required=True)
    parser.add_argument(
        "--addr-mapper",
        default="MOP4CLXOR",
        choices=sorted(ADDR_MAPPER_ALIASES),
    )
    parser.add_argument("--refresh-manager", default="NoRefresh")
    parser.add_argument("--channel-mapper", default="CacheLineInterleave")
    parser.add_argument("--clock-ratio", type=int, default=1)
    parser.add_argument("--duration", default="100us")
    parser.add_argument("--memory-size", default="128MiB")
    parser.add_argument("--block-size", type=int)
    parser.add_argument("--read-buffer-size", type=int)
    parser.add_argument("--write-buffer-size", type=int)
    parser.add_argument(
        "--traffic",
        nargs="+",
        choices=sorted(TRAFFIC_ALIASES),
        default=["stream", "random"],
    )
    parser.add_argument(
        "--read-ratios",
        type=int,
        nargs="+",
        default=DEFAULT_READ_RATIOS,
    )
    parser.add_argument(
        "--intensities",
        type=float,
        nargs="+",
        default=DEFAULT_INTENSITIES,
    )
    parser.add_argument(
        "--gem5-bin",
        default=os.environ.get("GEM5_BIN", "build/X86/gem5.opt"),
        help="gem5 binary path. Defaults to GEM5_BIN or build/X86/gem5.opt.",
    )
    parser.add_argument(
        "--ramulator2-home",
        type=Path,
        default=REPO_ROOT,
        help="Ramulator2 repo/library root.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        help="Output directory. Default: /tmp/gem5-pytrafficgen/<DRAM>_<MAPPER>_ch<N>/.",
    )
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def resolve_profile_spec(profile):
    std_cls = DRAMStandard._registry[profile["dram_class"]]
    dram_cls = getattr(ramulator.dram, profile["dram_class"])
    dram = dram_cls(
        org_preset=profile["org_preset"],
        timing_preset=profile["timing_preset"],
    )
    org, timing = dram.resolve()
    channel_width = org["channel_width"]
    bytes_per_req = (
        std_cls.data_payload_bytes
        or channel_width * std_cls.internal_prefetch_size // 8
    )

    tck_ns = timing["tCK_ps"] / 1000.0
    burst_gap = timing.get("nBL_min", timing.get("nBL"))
    if std_cls.data_payload_bytes is not None and burst_gap is not None:
        peak_gbps = bytes_per_req / (burst_gap * tck_ns)
    else:
        peak_gbps = channel_width * timing["rate"] / 8 / 1000

    if "PseudoChannel" in std_cls.levels:
        peak_gbps *= org.get("pseudochannel", 2)

    return ProfileSpec(
        bytes_per_req=bytes_per_req,
        peak_gbps_per_channel=peak_gbps,
    )


def normalize_args(args):
    args.addr_mapper = ADDR_MAPPER_ALIASES[args.addr_mapper]
    args.ramulator2_home = args.ramulator2_home.resolve()
    args.gem5_bin = Path(args.gem5_bin).resolve()
    if args.out_dir is None:
        args.out_dir = Path("/tmp/gem5-pytrafficgen") / (
            f"{args.dram}_{args.addr_mapper}_ch{args.channels}"
        )
    else:
        args.out_dir = args.out_dir.resolve()
    return args


def validate_args(args):
    if args.channels <= 0:
        raise ValueError("--channels must be positive")
    if args.channels > 1 and (args.channels & (args.channels - 1)) != 0:
        raise ValueError("--channels must be a power of two")
    if args.clock_ratio <= 0:
        raise ValueError("--clock-ratio must be positive")
    if args.timeout <= 0:
        raise ValueError("--timeout must be positive")
    if args.block_size is not None and args.block_size <= 0:
        raise ValueError("--block-size must be positive")
    if args.read_buffer_size is not None and args.read_buffer_size <= 0:
        raise ValueError("--read-buffer-size must be positive")
    if args.write_buffer_size is not None and args.write_buffer_size <= 0:
        raise ValueError("--write-buffer-size must be positive")
    for read_ratio in args.read_ratios:
        if read_ratio < 0 or read_ratio > 100:
            raise ValueError("--read-ratios values must be between 0 and 100")
    for intensity in args.intensities:
        if intensity <= 0:
            raise ValueError("--intensities values must be positive")
    if not args.gem5_bin.exists():
        raise FileNotFoundError(f"gem5 binary not found: {args.gem5_bin}")
    if not CONFIG_PATH.exists():
        raise FileNotFoundError(f"gem5 config not found: {CONFIG_PATH}")
    if not args.ramulator2_home.exists():
        raise FileNotFoundError(f"Ramulator2 home not found: {args.ramulator2_home}")
    if args.out_dir.exists() and any(args.out_dir.iterdir()):
        if not args.force:
            raise FileExistsError(
                f"output directory is not empty: {args.out_dir}; use --force"
            )
        shutil.rmtree(args.out_dir)
    args.out_dir.mkdir(parents=True, exist_ok=True)


def prepend_env_path(env, key, values):
    existing = env.get(key)
    prefix = os.pathsep.join(str(v) for v in values)
    env[key] = prefix if not existing else prefix + os.pathsep + existing


def gem5_env(args):
    env = os.environ.copy()
    prepend_env_path(
        env,
        "PYTHONPATH",
        [args.ramulator2_home / "python", args.ramulator2_home],
    )
    prepend_env_path(env, "LD_LIBRARY_PATH", [args.ramulator2_home])
    return env


def format_intensity(value):
    return f"{value:g}".replace(".", "p")


def parse_float(value):
    if value.lower() == "nan":
        return math.nan
    return float(value)


def parse_stats(stats_path):
    stats = {}
    for line in stats_path.read_text().splitlines():
        match = STAT_FLOAT_RE.match(line.strip())
        if match:
            stats[match.group(1)] = parse_float(match.group(2))
    return stats


def require_stat(stats, name):
    if name not in stats:
        raise KeyError(f"missing gem5 stat {name}")
    return stats[name]


def parse_ramulator_yaml(path):
    if not path.exists() or path.stat().st_size == 0:
        raise FileNotFoundError(f"missing or empty Ramulator stats: {path}")
    with path.open() as fh:
        data = yaml.safe_load(fh)
    if not data:
        raise ValueError(f"empty Ramulator stats YAML: {path}")
    return data


def parse_run_outputs(run_dir):
    stats_path = run_dir / "stats.txt"
    if not stats_path.exists() or stats_path.stat().st_size == 0:
        raise FileNotFoundError(f"missing or empty gem5 stats: {stats_path}")
    parse_ramulator_yaml(run_dir / "ramulator_stats.yaml")

    stats = parse_stats(stats_path)
    sim_freq = require_stat(stats, "simFreq")
    read_bw = require_stat(stats, "board.processor.cores.generator.readBW")
    write_bw = require_stat(stats, "board.processor.cores.generator.writeBW")
    avg_read_latency_ticks = require_stat(
        stats, "board.processor.cores.generator.avgReadLatency"
    )
    avg_write_latency_ticks = require_stat(
        stats, "board.processor.cores.generator.avgWriteLatency"
    )
    total_reads = require_stat(stats, "board.processor.cores.generator.totalReads")
    total_writes = require_stat(stats, "board.processor.cores.generator.totalWrites")
    return {
        "read_bw_Bps": read_bw,
        "write_bw_Bps": write_bw,
        "observed_gbps": (read_bw + write_bw) / 1e9,
        "avg_read_latency_ns": avg_read_latency_ticks / sim_freq * 1e9,
        "avg_write_latency_ns": avg_write_latency_ticks / sim_freq * 1e9,
        "total_reads": int(total_reads),
        "total_writes": int(total_writes),
    }


def base_row(args, profile, spec):
    block_size = args.block_size if args.block_size is not None else spec.bytes_per_req
    return {
        "dram": args.dram,
        "dram_class": profile["dram_class"],
        "org_preset": profile["org_preset"],
        "timing_preset": profile["timing_preset"],
        "controller_class": profile["controller_class"],
        "scheduler_class": profile["scheduler_class"],
        "row_policy": profile["row_policy"],
        "channels": args.channels,
        "duration": args.duration,
        "memory_size": args.memory_size,
        "addr_mapper": args.addr_mapper,
        "refresh_manager": args.refresh_manager,
        "channel_mapper": args.channel_mapper,
        "clock_ratio": args.clock_ratio,
        "block_size": block_size,
        "read_buffer_size": args.read_buffer_size,
        "write_buffer_size": args.write_buffer_size,
    }


def make_command(args, row, traffic):
    cmd = [
        str(args.gem5_bin),
        "-d",
        row["run_dir"],
        str(CONFIG_PATH),
        "--dram-class",
        row["dram_class"],
        "--org-preset",
        row["org_preset"],
        "--timing-preset",
        row["timing_preset"],
        "--controller-class",
        row["controller_class"],
        "--scheduler-class",
        row["scheduler_class"],
        "--row-policy",
        row["row_policy"],
        "--channels",
        str(row["channels"]),
        "--rate-bps",
        str(row["rate_bps"]),
        "--block-size",
        str(row["block_size"]),
        "--duration",
        row["duration"],
        "--memory-size",
        row["memory_size"],
        "--read-ratio",
        str(row["read_ratio"]),
        "--traffic",
        TRAFFIC_ALIASES[traffic],
        "--addr-mapper",
        row["addr_mapper"],
        "--refresh-manager",
        row["refresh_manager"],
        "--channel-mapper",
        row["channel_mapper"],
        "--clock-ratio",
        str(row["clock_ratio"]),
    ]
    if row["read_buffer_size"] is not None:
        cmd.extend(["--read-buffer-size", str(row["read_buffer_size"])])
    if row["write_buffer_size"] is not None:
        cmd.extend(["--write-buffer-size", str(row["write_buffer_size"])])
    return cmd


def run_gem5_point(args, row, traffic):
    run_dir = Path(row["run_dir"])
    run_dir.mkdir(parents=True, exist_ok=True)
    cmd = make_command(args, row, traffic)
    (run_dir / "command.json").write_text(json.dumps(cmd, indent=2) + "\n")
    (run_dir / "resolved_config.json").write_text(
        json.dumps(row, indent=2, sort_keys=True) + "\n"
    )

    print("Run config:")
    print(json.dumps(row, indent=2, sort_keys=True))
    print("Command:")
    print(" ".join(cmd))

    try:
        completed = subprocess.run(
            cmd,
            cwd=args.ramulator2_home,
            env=gem5_env(args),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=args.timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        (run_dir / "gem5.log").write_text(output)
        raise TimeoutError(f"gem5 timed out after {args.timeout}s: {run_dir}") from exc

    (run_dir / "gem5.log").write_text(completed.stdout)
    if completed.returncode != 0:
        tail = "\n".join(completed.stdout.splitlines()[-40:])
        raise RuntimeError(
            f"gem5 failed with exit code {completed.returncode}: {run_dir}\n{tail}"
        )
    return parse_run_outputs(run_dir)


def rows_for_sweep(args, profile, spec):
    rows = []
    common = base_row(args, profile, spec)
    peak_gbps = spec.peak_gbps_per_channel * args.channels
    total = len(args.traffic) * len(args.read_ratios) * len(args.intensities)
    index = 0
    for traffic in args.traffic:
        for read_ratio in args.read_ratios:
            for intensity in args.intensities:
                index += 1
                rate_bps = int(peak_gbps * 1e9 * intensity)
                if rate_bps <= 0:
                    raise ValueError("computed rate_bps must be positive")
                run_dir = (
                    args.out_dir
                    / "runs"
                    / traffic
                    / f"rr{read_ratio}"
                    / f"i{format_intensity(intensity)}"
                )
                row = {
                    **common,
                    "traffic": traffic,
                    "read_ratio": read_ratio,
                    "intensity": intensity,
                    "rate_bps": rate_bps,
                    "peak_gbps": peak_gbps,
                    "run_dir": str(run_dir),
                }
                print(
                    f"[run {index}/{total}] "
                    f"{traffic} ch={args.channels} rr={read_ratio} "
                    f"intensity={intensity:g}"
                )
                parsed = run_gem5_point(args, row, traffic)
                rows.append({**row, **parsed})
    return rows


def write_csv(path, rows):
    with path.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def print_global_config(args, profile, spec):
    block_size = args.block_size if args.block_size is not None else spec.bytes_per_req
    config = {
        "dram": args.dram,
        **profile,
        "channels": args.channels,
        "traffic": args.traffic,
        "read_ratios": args.read_ratios,
        "intensities": args.intensities,
        "duration": args.duration,
        "memory_size": args.memory_size,
        "block_size": block_size,
        "addr_mapper": args.addr_mapper,
        "refresh_manager": args.refresh_manager,
        "channel_mapper": args.channel_mapper,
        "clock_ratio": args.clock_ratio,
        "read_buffer_size": args.read_buffer_size,
        "write_buffer_size": args.write_buffer_size,
        "peak_gbps": spec.peak_gbps_per_channel * args.channels,
        "peak_gbps_per_channel": spec.peak_gbps_per_channel,
        "gem5_bin": str(args.gem5_bin),
        "ramulator2_home": str(args.ramulator2_home),
        "out_dir": str(args.out_dir),
    }
    print("Resolved sweep config:")
    print(json.dumps(config, indent=2, sort_keys=True))
    (args.out_dir / "resolved_sweep_config.json").write_text(
        json.dumps(config, indent=2, sort_keys=True) + "\n"
    )


def main():
    args = normalize_args(parse_args())
    validate_args(args)
    profile = DRAM_PROFILES[args.dram]
    spec = resolve_profile_spec(profile)

    print(f"Output directory: {args.out_dir}")
    print_global_config(args, profile, spec)
    rows = rows_for_sweep(args, profile, spec)
    results_csv = args.out_dir / "results.csv"
    write_csv(results_csv, rows)
    print(f"Wrote CSV: {results_csv}")


if __name__ == "__main__":
    main()
