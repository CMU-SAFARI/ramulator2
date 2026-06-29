"""gem5 PyTrafficGen config for Ramulator2.

Run with a gem5 binary built with the Ramulator2 wrapper installed.
"""

import argparse
import json

import ramulator
from gem5.components.boards.test_board import TestBoard
from gem5.components.processors.linear_generator import LinearGenerator
from gem5.components.processors.random_generator import RandomGenerator
from gem5.simulate.simulator import Simulator


def make_component(namespace, name):
    try:
        return getattr(namespace, name)()
    except AttributeError as exc:
        raise ValueError(f"unknown Ramulator component {name}") from exc


def make_controller(args):
    dram_cls = getattr(ramulator.dram, args.dram_class)
    dram = dram_cls(
        org_preset=args.org_preset,
        timing_preset=args.timing_preset,
    )

    controller_cls = getattr(ramulator.controller, args.controller_class)
    scheduler = make_component(ramulator.scheduler, args.scheduler_class)
    refresh_manager = make_component(ramulator.refresh_manager, args.refresh_manager)
    row_policy = make_component(ramulator.row_policy, args.row_policy)
    addr_mapper = make_component(ramulator.addr_mapper, args.addr_mapper)

    controller_kwargs = {}
    if args.read_buffer_size is not None:
        controller_kwargs["read_buffer_size"] = args.read_buffer_size
    if args.write_buffer_size is not None:
        controller_kwargs["write_buffer_size"] = args.write_buffer_size

    return controller_cls(
        dram=dram,
        scheduler=scheduler,
        refresh_manager=refresh_manager,
        row_policy=row_policy,
        addr_mapper=addr_mapper,
        **controller_kwargs,
    )


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dram-class", required=True)
    parser.add_argument("--org-preset", required=True)
    parser.add_argument("--timing-preset", required=True)
    parser.add_argument("--controller-class", required=True)
    parser.add_argument("--scheduler-class", required=True)
    parser.add_argument("--row-policy", required=True)
    parser.add_argument("--channels", type=int, required=True)
    parser.add_argument("--rate-bps", type=int, required=True)
    parser.add_argument("--block-size", type=int, required=True)

    parser.add_argument("--duration", default="100us")
    parser.add_argument("--memory-size", default="128MiB")
    parser.add_argument(
        "--read-ratio",
        "--rd-perc",
        dest="read_ratio",
        type=int,
        default=100,
        help="Read percentage for PyTrafficGen. Writes are 100 - read_ratio.",
    )
    parser.add_argument(
        "--traffic",
        choices=("linear", "random"),
        default="linear",
        help="PyTrafficGen traffic pattern.",
    )
    parser.add_argument("--addr-mapper", default="MOP4CLXOR")
    parser.add_argument("--refresh-manager", default="NoRefresh")
    parser.add_argument("--channel-mapper", default="CacheLineInterleave")
    parser.add_argument("--clock-ratio", type=int, default=1)
    parser.add_argument("--read-buffer-size", type=int)
    parser.add_argument("--write-buffer-size", type=int)
    return parser.parse_args()


def validate_args(args):
    if args.channels <= 0:
        raise ValueError("--channels must be positive")
    if args.channels > 1 and (args.channels & (args.channels - 1)) != 0:
        raise ValueError("--channels must be a power of two for CacheLineInterleave")
    if args.rate_bps <= 0:
        raise ValueError("--rate-bps must be positive")
    if args.block_size <= 0:
        raise ValueError("--block-size must be positive")
    if args.clock_ratio <= 0:
        raise ValueError("--clock-ratio must be positive")
    if not 0 <= args.read_ratio <= 100:
        raise ValueError("--read-ratio must be between 0 and 100")
    if args.read_buffer_size is not None and args.read_buffer_size <= 0:
        raise ValueError("--read-buffer-size must be positive")
    if args.write_buffer_size is not None and args.write_buffer_size <= 0:
        raise ValueError("--write-buffer-size must be positive")


def resolved_config(args):
    return {
        "dram_class": args.dram_class,
        "org_preset": args.org_preset,
        "timing_preset": args.timing_preset,
        "controller_class": args.controller_class,
        "scheduler_class": args.scheduler_class,
        "row_policy": args.row_policy,
        "refresh_manager": args.refresh_manager,
        "addr_mapper": args.addr_mapper,
        "channel_mapper": args.channel_mapper,
        "clock_ratio": args.clock_ratio,
        "channels": args.channels,
        "traffic": args.traffic,
        "read_ratio": args.read_ratio,
        "rate_bps": args.rate_bps,
        "duration": args.duration,
        "memory_size": args.memory_size,
        "block_size": args.block_size,
        "read_buffer_size": args.read_buffer_size,
        "write_buffer_size": args.write_buffer_size,
    }


def main():
    args = parse_args()
    validate_args(args)

    mem_sys = ramulator.memory_system.GenericDRAM(
        clock_ratio=args.clock_ratio,
        controllers=[make_controller(args) for _ in range(args.channels)],
        channel_mapper=make_component(ramulator.channel_mapper, args.channel_mapper),
    )
    memory = ramulator.gem5.Memory(mem_sys, size=args.memory_size)

    generator_cls = LinearGenerator if args.traffic == "linear" else RandomGenerator
    generator = generator_cls(
        num_cores=1,
        duration=args.duration,
        rate=f"{args.rate_bps}B/s",
        block_size=args.block_size,
        min_addr=0,
        max_addr=memory.get_size(),
        rd_perc=args.read_ratio,
        data_limit=0,
    )

    board = TestBoard(
        clk_freq="3GHz",
        generator=generator,
        memory=memory,
        cache_hierarchy=None,
    )

    print("PyTrafficGen Ramulator2 Config:")
    print(json.dumps(resolved_config(args), sort_keys=True, indent=2))

    simulator = Simulator(board=board)
    simulator.run()
    print("Simulation complete")


if __name__ == "__m5_main__":
    main()
