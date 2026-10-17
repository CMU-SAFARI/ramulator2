"""Generate the visualizer's example traces (RAM2BIN) and an index.json manifest.

    ramulator2-venv/bin/python visualizer/scripts/gen_example_traces.py [out_dir]

Output: visualizer/public/examples/ by default, served by the app at /examples/.
Each example drives one controller configuration with BinTraceRecorder attached. The read-only
HBM4 sparse gather replays a synthetic address-vector gather (ReadWriteTrace). Examples with writes
use LatencyThroughputTrace instead: write forwarding / coalescing in ControllerBase keys on the flat
request address, which address-vector traces leave at -1.
"""
import json
import random
import struct
import sys
import tempfile
from pathlib import Path

import ramulator

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent))
from tests.utils import extract_dram_layout  # noqa: E402
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "public" / "examples"


def level_sizes(dram):
    """(level names, sizes) of the address vector, Channel pinned to 1."""
    names = list(type(dram).levels.keys())
    org, _ = dram.resolve()
    return names, [1 if n == "Channel" else org.get(n.lower(), 1) for n in names]


def gather(names, sizes, rng, n, group, write_frac, fixed=None, rows=4096):
    """Sparse gather: random bank and row, `group` consecutive columns per visit."""
    fixed = fixed or {}
    out = []
    while len(out) < n:
        addr = [fixed.get(name, rng.randrange(size)) for name, size in zip(names, sizes)]
        addr[names.index("Row")] = rng.randrange(min(rows, sizes[names.index("Row")]))
        ci = names.index("Column")
        base = rng.randrange(sizes[ci] // group) * group
        op = "W" if rng.random() < write_frac else "R"
        for k in range(group):
            addr[ci] = base + k
            out.append(f"{op} {','.join(map(str, addr))}")
    return out[:n]


def ltt(dram, read_ratio, stream_cls, probes=150, warmup=1500, nop=10, clock_ratio=4):
    """LatencyThroughputTrace (random probes over a streaming background), as in tests/smoke."""
    return ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=clock_ratio, nop_counter=nop, num_probe_requests=probes, warmup_cycles=warmup,
        seed=1, read_ratio=read_ratio, stream_cls=stream_cls, **extract_dram_layout(dram))


R = ramulator
EXAMPLES = [
    dict(
        file="hbm4_8000_sparse_gather",
        title="HBM4 8 Gbps · sparse gather",
        description="1 SID, one pseudo-channel, 256 B gathers, closed-row cap 8, 64-entry queue, read-only, refresh off.",
        dram=lambda: R.dram.HBM4(org_preset="HBM4_32Gb_4Hi", timing_preset="HBM4_8000Mbps"),
        ctrl=lambda d, plugins: R.controller.HBM34(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.ClosedCAP(cap=8),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.NoRefresh(),
            read_buffer_size=64, write_buffer_size=64, controller_plugins=plugins),
        pattern=lambda names, sizes, rng: gather(names, sizes, rng, 20000, 8, 0.0, fixed={"PseudoChannel": 0}),
    ),
    dict(
        file="hbm4_8000_2sid_mixed_refresh",
        title="HBM4 8 Gbps · 2 SIDs, 25% writes, per-bank refresh",
        description="Both pseudo-channels, streaming background with random probes, writes, REFpb.",
        dram=lambda: R.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps"),
        ctrl=lambda d, plugins: R.controller.HBM34(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.ClosedCAP(cap=8),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.HBM34PerBankRefresh(),
            read_buffer_size=64, write_buffer_size=64, controller_plugins=plugins),
        frontend=lambda d: ltt(d, 75, 32),
    ),
    dict(
        file="ddr5_4800_2rank_mixed_refresh",
        title="DDR5-4800 · 2 ranks, 30% writes, all-bank refresh",
        description="Open-row streams across two ranks; REFab with PREab fan-out, read/write turnarounds.",
        dram=lambda: R.dram.DDR5(org_preset="DDR5_16Gb_x8", timing_preset="DDR5_4800AN", rank=2),
        ctrl=lambda d, plugins: R.controller.GenericDDR(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.Open(),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.AllBank(),
            controller_plugins=plugins),
        frontend=lambda d: ltt(d, 70, 8),
    ),
    dict(
        file="ddr4_2400_stream_openrow",
        title="DDR4-2400 · open-row streaming, heavy load",
        description="High data-bus utilisation from long row hits; tFAW and bank-group (tCCD_L) blocking visible.",
        dram=lambda: R.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R"),
        ctrl=lambda d, plugins: R.controller.GenericDDR(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.Open(),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.NoRefresh(),
            controller_plugins=plugins),
        frontend=lambda d: ltt(d, 90, 8, nop=2),
    ),
    dict(
        file="lpddr5_6400_mixed",
        title="LPDDR5-6400 · two-part activates, 25% writes",
        description="ACT1/ACT2 activation, CAS sync commands, closed-row cap 4, 25% writes.",
        dram=lambda: R.dram.LPDDR5(org_preset="LPDDR5_8Gb_x16", timing_preset="LPDDR5_6400"),
        ctrl=lambda d, plugins: R.controller.LPDDR5(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.ClosedCAP(cap=4),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.NoRefresh(),
            controller_plugins=plugins),
        frontend=lambda d: ltt(d, 75, 8),
    ),
    dict(
        file="gddr7_28000_mixed",
        title="GDDR7 28 Gbps PAM3 · 20% writes",
        description="No bank groups and no tFAW: the bank-group and tFAW lanes are omitted.",
        dram=lambda: R.dram.GDDR7(org_preset="GDDR7_16Gb_x8", timing_preset="GDDR7_28000_PAM3"),
        ctrl=lambda d, plugins: R.controller.GDDR7(
            dram=d, scheduler=R.scheduler.FRFCFS(), row_policy=R.row_policy.ClosedCAP(cap=4),
            addr_mapper=R.addr_mapper.PassThroughAddrMapper(), refresh_manager=R.refresh_manager.NoRefresh(),
            controller_plugins=plugins),
        frontend=lambda d: ltt(d, 80, 64),
    ),
]


def num_entries(path):
    with open(path, "rb") as f:
        head = f.read(64)
    return struct.unpack_from("<Q", head, 32)[0]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = []
    with tempfile.TemporaryDirectory() as tmp:
        for ex in EXAMPLES:
            dram = ex["dram"]()
            if "pattern" in ex:
                names, sizes = level_sizes(dram)
                trace = Path(tmp) / f"{ex['file']}.trace"
                trace.write_text("\n".join(ex["pattern"](names, sizes, random.Random(ex["file"]))) + "\n")
                frontend = R.frontend.ReadWriteTrace(clock_ratio=1, path=str(trace))
            else:
                frontend = ex["frontend"](dram)
            prefix = OUT / ex["file"]
            plugin = R.controller_plugin.BinTraceRecorder(path=str(prefix), dram_type=type(dram).name)
            mem = R.memory_system.GenericDRAM(
                clock_ratio=1, controllers=[ex["ctrl"](dram, [plugin])],
                channel_mapper=R.channel_mapper.PassThroughChannelMapper())
            R.Simulation(frontend, mem).run()
            out = Path(f"{prefix}.ch0.ram2bin")
            entry = dict(file=out.name, title=ex["title"], description=ex["description"],
                         standard=type(dram).name, commands=num_entries(out), bytes=out.stat().st_size)
            manifest.append(entry)
            print(f"{out.name}: {entry['commands']} commands, {entry['bytes'] / 1e6:.1f} MB")
    (OUT / "index.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {OUT / 'index.json'}")


if __name__ == "__main__":
    main()
