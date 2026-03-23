"""Shared simulation runners for test suites.

Provides functions to build, run, and collect results from
LatencyThroughputTrace simulations. Used by both smoke and
latency-throughput test suites.
"""

from tests.utils.dram import create_dram


def extract_dram_layout(dram):
    """Extract DRAM hierarchy layout for LatencyThroughputTrace address generation.

    Takes a Python DRAM spec object and returns a dict of kwargs to pass
    to LatencyThroughputTrace (addr_vec_size, bank_positions, bank_counts,
    total_bank_units, row_pos, col_pos, num_rows, num_cols).
    """
    cls = type(dram)
    level_names = list(cls.levels.keys())
    org_dict, _ = dram.resolve()
    org_counts = [org_dict.get(name.lower(), 1) for name in level_names]

    row_idx = level_names.index("Row")
    col_idx = level_names.index("Column")

    # Bank positions: all levels between Channel (0) and Row
    bank_positions = list(range(1, row_idx))
    bank_counts = [org_counts[i] for i in bank_positions]

    # Reorder bank_positions so interleaving-critical levels cycle fastest
    # (last entry in bank_positions = fastest cycling in decompose_bank).

    # BankGroup → cycle fast to get nCCDS (different-BG) instead of nCCDL (same-BG).
    # For HBM1: nCCDS=1 vs nCCDL=2 → 2x penalty without this reorder.
    if "BankGroup" in level_names:
        bg_idx_in_banks = level_names.index("BankGroup") - 1  # offset by Channel
        if bg_idx_in_banks < len(bank_positions) - 1:
            pos = bank_positions.pop(bg_idx_in_banks)
            cnt = bank_counts.pop(bg_idx_in_banks)
            bank_positions.append(pos)
            bank_counts.append(cnt)

    # PseudoChannel level → cycle fastest (independent timing domains).
    if "PseudoChannel" in level_names:
        pc_idx_in_banks = [
            i for i, p in enumerate(bank_positions) if p == level_names.index("PseudoChannel")
        ][0]
        pos = bank_positions.pop(pc_idx_in_banks)
        cnt = bank_counts.pop(pc_idx_in_banks)
        bank_positions.append(pos)
        bank_counts.append(cnt)

    total_bank_units = 1
    for c in bank_counts:
        total_bank_units *= c

    return {
        "addr_vec_size": len(level_names),
        "bank_positions": bank_positions,
        "bank_counts": bank_counts,
        "total_bank_units": total_bank_units,
        "row_pos": row_idx,
        "col_pos": col_idx,
        "num_rows": org_counts[row_idx],
        "num_cols": org_counts[col_idx],
    }


def build_controller(cfg, dram, *, full=False):
    """Instantiate the configured controller for fast or full validation."""
    import ramulator

    ctrl_cls = getattr(ramulator.controller, cfg["controller_class"])
    ctrl_kwargs = dict(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFS(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.DirectMapper(),
    )
    extra_key = "full_ctrl_extra_kwargs" if full else "fast_ctrl_extra_kwargs"
    ctrl_kwargs.update(cfg[extra_key])
    return ctrl_cls(**ctrl_kwargs)


def run_single(
    std_name,
    nop_counter,
    read_ratio=100,
    num_probes=10000,
    warmup=10000,
    clock_ratio=None,
    stream_cols=None,
    full=False,
):
    """Run one simulation point and return sim.stats.

    This is a top-level function (not a closure) so it can be pickled
    for ProcessPoolExecutor.  It re-imports ramulator in each subprocess.
    """
    import ramulator

    from tests.latency_throughput.testcases import STANDARDS

    cfg = STANDARDS[std_name]
    if clock_ratio is None:
        clock_ratio = cfg["frontend_clock_ratio"]
    if stream_cols is None:
        stream_cols = cfg["stream_cols"]

    # Instantiate DRAM
    dram = create_dram(cfg)

    # Instantiate frontend
    layout = extract_dram_layout(dram)
    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=clock_ratio,
        nop_counter=nop_counter,
        num_probe_requests=num_probes,
        warmup_cycles=warmup,
        seed=12345,
        read_ratio=read_ratio,
        stream_cols=stream_cols,
        **layout,
    )

    ctrl = build_controller(cfg, dram, full=full)

    # Memory system
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
    )

    # Run
    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats


def run_streaming_only(std_name, num_requests=50000, full=False):
    """Run a streaming-only simulation (no probes) at maximum throughput.

    Fires sequential read requests as fast as the memory system can accept
    them, with no probe interference or NOP rate-limiting.

    Returns the stats dict from the completed simulation.
    """
    import ramulator

    from tests.latency_throughput.testcases import STANDARDS

    cfg = STANDARDS[std_name]
    clock_ratio = cfg["frontend_clock_ratio"]
    stream_cols = cfg["stream_cols"]

    # Instantiate DRAM
    dram = create_dram(cfg)

    # Instantiate frontend in streaming-only mode
    layout = extract_dram_layout(dram)
    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=clock_ratio,
        nop_counter=1,
        num_probe_requests=0,
        streaming_only=True,
        num_streaming_requests=num_requests,
        read_ratio=100,
        stream_cols=stream_cols,
        **layout,
    )

    ctrl = build_controller(cfg, dram, full=full)

    # Memory system
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
    )

    # Run
    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    return sim.stats
