#!/usr/bin/env python3
"""Generate a simple instruction trace for SimpleO3.

Each line is: <bubble_count> <load_addr> <store_addr>

python3 examples/generate_inst_trace.py 10000
python3 examples/generate_inst_trace.py 10000 --output my_trace.trace --max-addr 33554432
python3 examples/generate_inst_trace.py --seed 42

Defaults:
 - entries: 10000
 - output: examples/traces/example_inst.trace
 - bubble_count: random 0..10
 - addresses: random integers in [0, max_addr)
"""

from __future__ import annotations

import argparse
import random
from pathlib import Path


DEFAULT_COUNT = 10000
DEFAULT_OUTPUT = "dummy_inst.trace"
DEFAULT_MAX_ADDR = 536870912


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a SimpleO3 instruction trace in examples/traces/.")
    parser.add_argument(
        "count",
        nargs="?",
        type=int,
        default=DEFAULT_COUNT,
        help=f"number of trace entries to generate (default: {DEFAULT_COUNT})",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="output trace filename inside examples/traces/ (default: dummy_inst.trace)",
    )
    parser.add_argument(
        "--max-addr",
        type=int,
        default=DEFAULT_MAX_ADDR,
        help=f"exclusive upper bound for generated addresses (default: {DEFAULT_MAX_ADDR})",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="optional RNG seed for reproducible traces",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    if args.count <= 0:
        raise SystemExit("count must be positive")
    if args.max_addr <= 0:
        raise SystemExit("max-addr must be positive")

    if args.seed is not None:
        random.seed(args.seed)

    script_dir = Path(__file__).resolve().parent
    trace_dir = script_dir / "traces"
    trace_dir.mkdir(parents=True, exist_ok=True)

    output_name = args.output or DEFAULT_OUTPUT
    output_path = trace_dir / output_name

    with output_path.open("w", encoding="utf-8") as trace_file:
        for _ in range(args.count):
            bubble = random.randint(0, 10)
            load_addr = random.randrange(0, args.max_addr)
            store_addr = random.randrange(0, args.max_addr)
            trace_file.write(f"{bubble} {load_addr} {store_addr}\n")

    print(f"Wrote {args.count} entries to {output_path}")


if __name__ == "__main__":
    main()
