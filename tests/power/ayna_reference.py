"""Run a workload through the Ayna plugin and through Ayna's own runners, on the same inputs.

Ayna's runners (HBM2_runner, HBM3_runner) are built from CMU-SAFARI/HBM-Power at 318d3d1 and found
through HBM_POWER_AYNA_BIN (the directory holding them) or HBM_POWER_AYNA (a checkout built into
build/bin). Their inputs are exported from the same DRAM object and plugin config the simulation
used (ramulator.power.ayna_config / ayna_trace), so nothing about the device is restated here.
"""

import json
import os
import random
import re
import subprocess
from collections import Counter
from dataclasses import dataclass

import ramulator
import ramulator.power
import tests.controller_scheduling.harness as cs

_PCH = re.compile(r"Channel (\d+) PCh (\d+) : (\d+) cmds, energy = ([0-9.eE+-]+) pJ")


def find_ayna_bin():
    """The directory with Ayna's runners, or None."""
    candidates = []
    if os.environ.get("HBM_POWER_AYNA_BIN"):
        candidates.append(os.environ["HBM_POWER_AYNA_BIN"])
    if os.environ.get("HBM_POWER_AYNA"):
        candidates.append(os.path.join(os.environ["HBM_POWER_AYNA"], "build", "bin"))
    for path in candidates:
        if os.path.isfile(os.path.join(path, "HBM3_runner")):
            return path
    return None


@dataclass(frozen=True)
class Workload:
    """A synthetic request stream sent through the controller-scheduling harness."""

    name: str
    requests: int = 2000
    request_interval: int = 1  # ticks between new requests (1 = as fast as accepted)
    tail_ticks: int = 2000  # idle ticks after the last request, so refreshes keep running
    read_ratio: float = 0.7
    row_reuse: float = 0.7  # chance a request goes to the bank's current row
    rows: int = 64
    closed_page: bool = False  # ClosedCAP(cap=1): every access becomes RDA / WRA
    refresh: bool = True  # AllBank refresh (REFab)
    warmup_ticks: int = 0  # reset the stats after this many ticks (0: no warmup)
    seed: int = 1


@dataclass
class PluginRun:
    stats: dict  # the Ayna plugin's stats
    commands: Counter  # issued commands by name
    window_start: int  # tick of the last stats reset
    end_tick: int
    cmd_trace: str
    power: dict  # the plugin's power config


def _controller_maker(dram):
    return (
        cs.ControllerUnderTest.make_hbm12
        if type(dram).name == "HBM2"
        else cs.ControllerUnderTest.make_hbm34
    )


def run_workload(dram, workload, workdir, stop_at=None, **ayna_kwargs):
    """Run ``workload`` with the Ayna plugin and a CmdTraceRecorder attached.

    ``stop_at`` ends the run after that many ticks, wherever the workload is. ``ayna_kwargs`` go to
    ``ramulator.power.ayna_power``.
    """
    power = ramulator.power.ayna_power(**ayna_kwargs)
    cmd_trace = os.path.join(workdir, "cmd.csv")
    recorder = ramulator.controller_plugin.CmdTraceRecorder(path=cmd_trace)
    dut = _controller_maker(dram)(
        dram,
        row_policy=ramulator.row_policy.ClosedCAP(cap=1)
        if workload.closed_page
        else ramulator.row_policy.Open(),
        refresh_manager=ramulator.refresh_manager.AllBank()
        if workload.refresh
        else ramulator.refresh_manager.NoRefresh(),
        controller_plugins=[recorder, ramulator.controller_plugin.Ayna(power=power)],
    )

    org, _ = dram.resolve()
    rng = random.Random(workload.seed)
    open_row = {}
    requests = []
    for _ in range(workload.requests):
        bank = (
            rng.randrange(org["pseudochannel"]),
            rng.randrange(org["sid"]),
            rng.randrange(org["bankgroup"]),
            rng.randrange(org["bank"]),
        )
        if bank not in open_row or rng.random() >= workload.row_reuse:
            open_row[bank] = rng.randrange(workload.rows)
        pc, sid, bg, bk = bank
        addr = dut.addr_vec(
            Channel=0,
            PseudoChannel=pc,
            Sid=sid,
            BankGroup=bg,
            Bank=bk,
            Row=open_row[bank],
            Column=rng.randrange(org["column"] // 8),
        )
        requests.append(("Read" if rng.random() < workload.read_ratio else "Write", addr))

    tick = 0

    def advance():
        nonlocal tick
        dut.tick()
        tick += 1
        if tick == workload.warmup_ticks:
            dut.reset_stats()

    while requests and tick != stop_at:
        if tick % workload.request_interval == 0:
            try:
                dut.send_request(*requests[0])
                requests.pop(0)
            except RuntimeError:
                pass  # buffer full: retry on a later tick
        advance()
    for _ in range(workload.tail_ticks):
        if tick == stop_at:
            break
        advance()

    stats = dut.stats()  # finalizes: the recorder closes its file, the plugin updates its stats
    plugins = stats["controller_plugin"]
    plugins = plugins if isinstance(plugins, list) else [plugins]
    plugin = next(p for p in plugins if p["impl"] == "Ayna")
    return PluginRun(
        stats=plugin,
        commands=_count_commands(cmd_trace + ".ch0"),
        window_start=workload.warmup_ticks,
        end_tick=workload.warmup_ticks + plugin["cycles"],
        cmd_trace=cmd_trace + ".ch0",
        power=power,
    )


def _count_commands(path):
    # Every issued command, including refreshes and policy-injected precharges.
    with open(path) as f:
        next(f)
        return Counter(line.split(",")[1] for line in f)


def run_ayna(ayna_bin, dram, run, workdir):
    """Ayna's energy per pseudo-channel over the run's stats window: {pc: (commands, energy_pJ)}.

    With a warmup, the window's energy is Ayna's energy at the end minus its energy at the reset.
    """
    end, ncmds = _run_ayna_at(ayna_bin, dram, run, workdir, run.end_tick)
    if run.window_start == 0:
        return {pc: (ncmds[pc], e) for pc, e in end.items()}
    start, start_cmds = _run_ayna_at(ayna_bin, dram, run, workdir, run.window_start)
    return {pc: (ncmds[pc] - start_cmds[pc], end[pc] - start[pc]) for pc in end}


def _run_ayna_at(ayna_bin, dram, run, workdir, end_tick):
    cfg = ramulator.power.ayna_config(dram, run.power, run.stats)
    files = {}
    for key in ("organization", "timing", "power", "variation"):
        if cfg[key] is not None:
            files[key] = os.path.join(workdir, f"{key}.json")
            with open(files[key], "w") as f:
                json.dump(cfg[key], f, indent=1)
    trace = os.path.join(workdir, "ayna_trace.csv")
    with open(trace, "w") as f:
        f.write(ramulator.power.ayna_trace(run.cmd_trace, dram, end_tick))

    model = run.power["standards"][type(dram).name]["model"]
    runner = "HBM2_runner" if model == "HBM2" else "HBM3_runner"
    args = [
        os.path.join(ayna_bin, runner),
        files["organization"],
        files["timing"],
        files["power"],
        trace,
    ]
    if "variation" in files:
        args.append(files["variation"])
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    matches = list(_PCH.finditer(out))
    energy = {int(m.group(2)): float(m.group(4)) for m in matches}
    commands = {int(m.group(2)): int(m.group(3)) - 1 for m in matches}  # minus the closing NOP
    return energy, commands
