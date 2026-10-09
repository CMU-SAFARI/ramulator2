"""Inputs for Ayna's own runners, derived from the same objects the plugin reads.

Ramulator's speedbin and organization stay the single source of truth: ``ayna_config`` writes
Ayna's organization, timing and power files from the DRAM object and the plugin's config and stats,
and ``ayna_trace`` turns a ``CmdTraceRecorder`` text trace into Ayna's trace format. Both use
controller ticks, the plugin's time unit, so Ayna's HBM2_runner / HBM3_runner see the same numbers
the plugin does.
"""

import csv
from collections import defaultdict

# Ramulator command -> Ayna trace mnemonic (the commands the plugin models)
AYNA_COMMANDS = {
    "ACT": "ACT",
    "PREpb": "PRE",
    "PREab": "PREA",
    "RD": "RD",
    "WR": "WR",
    "RDA": "RDA",
    "WRA": "WRA",
    "REFab": "REFA",
}


def _resolved(dram):
    cfg = dram.to_config()
    std = type(dram)
    timing = dict(zip(std.timing_params, cfg["timing"]))  # controller ticks
    org = dict(zip(std.levels, cfg["org"]["count"]))
    return std, cfg, timing, org


def ayna_config(dram, power, stats):
    """Ayna's three config files (plus an optional variation file) as dicts.

    ``power`` is the plugin's ``power`` config and ``stats`` its stats, which carry the electrical
    values the plugin used for ``dram``.
    Returns ``{"organization": ..., "timing": ..., "power": ..., "variation": ... or None}``.
    The SID level is flattened into the bank groups, as in the plugin.
    """
    std, cfg, timing, org = _resolved(dram)
    levels = list(std.levels)
    groups = 1
    for level in levels[levels.index("PseudoChannel") + 1 : levels.index("Bank")]:
        groups *= org[level]

    organization = {
        "stacks": 1,
        "channels_per_stack": 1,
        "pseudochannels_per_channel": org["PseudoChannel"],
        "bankgroups_per_pseudochannel": groups,
        "banks_per_bankgroup": org["Bank"],
        "rows": org["Row"],
        "columns": org["Column"],
        "width": cfg["org"]["dq"],
        "dataRate": std.internal_prefetch_size // timing["nBL"],  # transfers per tick
    }

    timing_json = {
        "timing": {
            "tCK_ps": timing["tCK_ps"],
            "nBL": std.internal_prefetch_size,  # beats per burst
            "nRAS": timing["nRAS"],
            "nRP": timing["nRP"],
            "nRC": timing["nRC"],
            "nRCD": timing["nRCDRD"],
            "nRCDWR": timing["nRCDWR"],
            "nRL": timing["nCL"],
            "nCL": timing["nCL"],
            "nWL": timing["nCWL"],
            "nWR": timing["nWR"],
            "nRFC": timing["nRFC"],
        }
    }

    dp = power["dataPattern"]
    datapattern_keys = {
        "enabled": "enabled",
        "floor_pJbit": "floor_pJbit",
        "c_dq": "coef_T_DQ",
        "c_t2bit": "coef_T_2bit",
        "c_busflip": "coef_busflip",
        "use_coupling": "use_coupling",
        "c_bg_coupling": "coef_BG_coupling",
        "c_tsv_coupling": "coef_TSV_coupling",
        "bgcpl_full": "bgcpl_full",
        "tsvcpl_full": "tsvcpl_full",
        "dq_rate": "dq_rate",
        "tsv_rate": "tsv_rate",
        "bg_rate": "bg_rate",
        "ref_dq_rate": "ref_dq_rate",
        "ref_tsv_rate": "ref_tsv_rate",
        "ref_bg_rate": "ref_bg_rate",
        "K": "K_mA_per_pJbit",
        "apply_to_writes": "apply_to_writes",
    }
    power_json = {
        "voltage": {"VDD": stats["vDD"], "VDDQ": stats["vDDQ"], "unit": "V"},
        "IDD": {
            "IDD0": stats["iDD0"],
            "IDD2N": stats["iDD2N"],
            "IDD3N1": stats["iDD3N1"],
            "IDD3N16": stats["iDD3N16"],
            "IDD4R": stats["iDD4R"],
            "IDD4W": stats["iDD4W"],
            "IDD5B": stats["iDD5B"],
            "unit": "mA",
        },
        "datapattern": {ayna_key: dp[key] for key, ayna_key in datapattern_keys.items()},
    }

    variation = None
    factor_keys = {
        "bankgroup_scaling_factors": "bankgroup",
        "bank_scaling_factors": "bank",
        "bankgroup_write_scaling_factors": "bankgroup_write",
        "bank_write_scaling_factors": "bank_write",
    }
    if power["variation"]:
        factors = power["standards"][std.name]["variation"]
        variation = {"scaling_factors": {name: factors[key] for key, name in factor_keys.items()}}

    return {
        "organization": organization,
        "timing": timing_json,
        "power": power_json,
        "variation": variation,
    }


def ayna_trace(cmd_trace_path, dram, end_tick, channel_id=0):
    """Ayna trace (CSV text) for one channel from a ``CmdTraceRecorder`` text trace.

    Each command is stamped at its completion tick (issue tick + command cycles - 1), as in the
    plugin, and only commands completed by ``end_tick`` are kept. Every pseudo-channel of the
    channel ends with a NOP at ``end_tick``, so idle pseudo-channels count too.
    """
    std, cfg, _, org = _resolved(dram)
    levels = list(std.levels)
    cycles = dict(zip(std.commands, cfg["command_cycles"]))
    d = levels.index("PseudoChannel")
    bank = levels.index("Bank")
    group_levels = levels[d + 1 : bank]

    per_pc = defaultdict(list)
    with open(cmd_trace_path, newline="") as f:
        for row in csv.DictReader(f):
            name = row["command"]
            if name not in AYNA_COMMANDS:
                continue
            completion = int(row["clock"]) + cycles[name] - 1
            if completion > end_tick:
                continue
            addr = {level: int(row[level]) for level in levels}
            group = 0
            for level in group_levels:  # SID flattened into the bank group index
                group = group * org[level] + max(addr[level], 0)
            per_pc[addr["PseudoChannel"]].append(
                (
                    completion,
                    AYNA_COMMANDS[name],
                    group,
                    max(addr["Bank"], 0),
                    max(addr["Row"], 0),
                    max(addr["Column"], 0),
                )
            )

    lines = ["timestamp,command,channel_id,pseudochannel_id,bankgroup_id,bank_id,row_id,column_id"]
    for pc in range(org["PseudoChannel"]):
        commands = sorted(per_pc.get(pc, []), key=lambda c: c[0])  # stable: issue order on ties
        for t, cmd, bg, bk, row, col in commands:
            lines.append(f"{t},{cmd},{channel_id},{pc},{bg},{bk},{row},{col}")
        lines.append(f"{end_tick},NOP,{channel_id},{pc},0,0,0,0")
    return "\n".join(lines) + "\n"
