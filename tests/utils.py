"""Shared test utilities — DRAM creation and layout extraction."""

import ramulator


def create_dram(cfg):
    """Instantiate a DRAM object from a testcase CONFIG dict."""
    dram_cls = getattr(ramulator.dram, cfg["dram_class"])
    return dram_cls(org_preset=cfg["org_preset"], timing_preset=cfg["timing_preset"], **cfg["dram_kwargs"])


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
