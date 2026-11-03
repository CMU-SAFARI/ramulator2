"""Trace plugins (BinTraceRecorder, LiveTraceStreamer) and the defaults codegen gives them."""

import array
import json
import socket
import struct
import subprocess
import sys
import textwrap
import time
from collections import defaultdict
from pathlib import Path

import ramulator
import ramulator.cpp_parser
from tests.utils import extract_dram_layout


REPO = Path(__file__).resolve().parents[2]


def run_ddr5(plugins, nop_counter=40, probes=200, finalize=True):
    dram = ramulator.dram.DDR5(org_preset="DDR5_16Gb_x8", timing_preset="DDR5_4800AN")
    frontend = ramulator.frontend.LatencyThroughputTrace(
        clock_ratio=8, nop_counter=nop_counter, read_ratio=70, num_probe_requests=probes,
        latency_sample_count=probes, stream_cls=64, **extract_dram_layout(dram))
    controller = ramulator.controller.GenericDDR(
        dram=dram,
        scheduler=ramulator.scheduler.FRFCFSRowHit(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        refresh_manager=ramulator.refresh_manager.AllBank(),
        controller_plugins=plugins if isinstance(plugins, list) else [plugins],
    )
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1, controllers=[controller],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper())
    sim = ramulator.Simulation(frontend, mem)
    sim.run()
    if finalize:
        sim.finalize()
    return sim


def read_ram2bin(path):
    """Header fields, command names and the chunks' columns of a RAM2BIN 1.2 file."""
    data = Path(path).read_bytes()
    flags, levels, ncmd = struct.unpack_from("<HHH", data, 10)
    num_entries, data_offset = struct.unpack_from("<QQ", data, 32)
    pos = 64
    for _ in range(levels):
        pos = data.index(b"\0", pos) + 1
    pos += 4 * levels
    names = []
    for _ in range(ncmd):
        end = data.index(b"\0", pos)
        names.append(data[pos:end].decode())
        pos = end + 1
    cols = {name: array.array(code) for name, code in
            (("clk", "q"), ("arrive", "q"), ("req_id", "q"), ("cmd", "B"))}
    chunks = []
    pos = data_offset
    while pos + 8 <= len(data):
        n, reserved = struct.unpack_from("<II", data, pos)
        end = pos + 8 + n * (28 + 4 * levels)
        if n == 0 or reserved != 0 or end > len(data):
            break
        start = pos + 8
        columns = ((0, "clk", 8), (8, "arrive", 8), (16, "req_id", 8), (24, "cmd", 1))
        for offset, name, size in columns:
            cols[name].frombytes(data[start + offset * n:start + (offset + size) * n])
        chunks.append(n)
        pos = end + (-end % 8)
    return dict(version=tuple(data[8:10]), flags=flags, num_entries=num_entries,
                data_offset=data_offset, names=names, chunks=chunks, **cols)


def test_parser_reads_defaults_containing_parentheses(tmp_path):
    src = tmp_path / "plugin.cpp"
    src.write_text(textwrap.dedent("""
        RAMULATOR_PARSE_PARAM(m_a, std::string, "a").default_val(std::string(""));
        RAMULATOR_PARSE_PARAM(m_b, std::string, "b").default_val(std::string("x(y)"));
        RAMULATOR_PARSE_PARAM(m_c, double, "c").default_val(0.5);
        RAMULATOR_PARSE_PARAM(m_d, std::string, "d")
            .default_val("random-probe");
        RAMULATOR_PARSE_PARAM(m_e, std::vector<int>, "e").default_val(std::vector<int>{});
        RAMULATOR_PARSE_PARAM(m_f, int, "f").required();
    """))
    params = {p["name"]: p for p in ramulator.cpp_parser.parse_params(str(src))}
    assert params["a"]["default_val"] == ""
    assert params["b"]["default_val"] == "x(y)"
    assert params["c"]["default_val"] == 0.5
    assert params["d"]["default_val"] == "random-probe"
    assert params["e"]["default_val"] == []
    assert params["f"]["required"]


def test_recorder_header_names_the_standard_by_default(tmp_path):
    run_ddr5(ramulator.controller_plugin.BinTraceRecorder(path=str(tmp_path / "t")))
    header = (tmp_path / "t.ch0.ram2bin").read_bytes()[:64]
    assert header[:8] == b"RAM2BIN\0"
    assert header[48:64].rstrip(b"\0") == b"DDR5"


def test_recorder_writes_chunks_tagged_with_request_ids(tmp_path):
    recorder = ramulator.controller_plugin.BinTraceRecorder(path=str(tmp_path / "t"))
    run_ddr5(recorder, nop_counter=10, probes=3000)
    t = read_ram2bin(tmp_path / "t.ch0.ram2bin")
    assert t["version"] == (1, 2)
    assert t["flags"] == 1  # complete
    assert len(t["chunks"]) > 1 and all(n == 65536 for n in t["chunks"][:-1])
    assert t["num_entries"] == sum(t["chunks"]) == len(t["clk"])

    requests = defaultdict(list)
    for i, arrive in enumerate(t["arrive"]):
        if arrive >= 0:
            assert t["req_id"][i] >= 0
            requests[t["req_id"][i]].append(i)
    column = {i for i, name in enumerate(t["names"]) if name in ("RD", "RDA", "WR", "WRA")}
    for cmds in requests.values():
        assert len({t["arrive"][i] for i in cmds}) == 1
        assert sum(t["cmd"][i] in column for i in cmds) <= 1


STOPPED_RUN = textwrap.dedent("""
    import os, sys
    sys.path[:0] = [sys.argv[1], sys.argv[2]]
    import ramulator
    from tests.unit_tests.test_trace_plugins import run_ddr5
    recorder = ramulator.controller_plugin.BinTraceRecorder(path=sys.argv[3])
    sim = run_ddr5(recorder, nop_counter=10, probes=3000, finalize=False)
    os._exit(0)  # ends the process without finalizing the simulation
""")


def test_recorder_keeps_the_complete_chunks_of_a_run_that_stops_early(tmp_path):
    args = [str(REPO / "python"), str(REPO), str(tmp_path / "t")]
    subprocess.run([sys.executable, "-c", STOPPED_RUN, *args], check=True, capture_output=True)
    t = read_ram2bin(tmp_path / "t.ch0.ram2bin")
    assert t["flags"] == 0 and t["num_entries"] == 0
    assert t["data_offset"] > 64
    assert t["chunks"] and all(n == 65536 for n in t["chunks"])


SINK = textwrap.dedent("""
    import json, sys
    from http.server import BaseHTTPRequestHandler, HTTPServer
    log = open(sys.argv[1], "w", buffering=1)
    reply = sys.argv[2].encode()
    class H(BaseHTTPRequestHandler):
        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"])).decode()
            log.write(json.dumps({"path": self.path, "auth": self.headers.get("Authorization"),
                                  "body": body}) + "\\n")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(reply)
        def log_message(self, *a):
            pass
    server = HTTPServer(("127.0.0.1", 0), H)
    print(server.server_port, flush=True)
    server.serve_forever()
""")


def stream_to_sink(tmp_path, reply='{"ok":true,"interrupted":false}', extra_plugins=(), env=None):
    """Streams the DDR5 run to a stand-in server; returns each post as (query, auth, body)."""
    log = tmp_path / "posts.log"
    sink = subprocess.Popen([sys.executable, "-c", SINK, str(log), reply],
                            stdout=subprocess.PIPE, text=True)
    try:
        port = int(sink.stdout.readline())
        streamer = ramulator.controller_plugin.LiveTraceStreamer(port=port)
        run_ddr5([streamer, *extra_plugins])
    finally:
        sink.kill()
        sink.wait()
    posts = []
    for line in log.read_text().splitlines():
        post = json.loads(line)
        query = dict(p.split("=", 1) for p in post["path"].split("?", 1)[1].split("&"))
        posts.append((query, post["auth"], post["body"]))
    return posts


def decode_events(bodies):
    """Undoes the streamer's column encoding: rows of (clk, arrive, req_id, cmd)."""
    rows = []
    for cols in map(json.loads, bodies):
        clk = req = 0
        for i in range(cols["n"]):
            clk += cols["clk"][i]
            req += cols["req"][i]
            wait = cols["wait"][i]
            rows.append((clk, -1 if wait < 0 else clk - wait, req, cols["cmd"][i]))
    return rows


def test_streamer_sends_what_the_recorder_records(tmp_path):
    recorder = ramulator.controller_plugin.BinTraceRecorder(path=str(tmp_path / "t"))
    posts = stream_to_sink(tmp_path, extra_plugins=[recorder])
    types = [q["type"] for q, _, _ in posts]
    assert types[0] == "init" and types[-1] == "done" and "events" in types
    init = json.loads(posts[0][2])
    assert init["header"]["dramType"] == "DDR5"
    assert all(q["session"] == f"{init['run']}:{init['channel']}" for q, _, _ in posts)

    events = [(q, body) for q, _, body in posts if q["type"] == "events"]
    assert all(int(q["count"]) == json.loads(body)["n"] for q, body in events)
    t = read_ram2bin(tmp_path / "t.ch0.ram2bin")
    recorded = list(zip(t["clk"], t["arrive"], t["req_id"], t["cmd"]))
    assert decode_events(body for _, body in events) == recorded


def test_streamer_stops_when_a_viewer_stops_it(tmp_path):
    posts = stream_to_sink(tmp_path, reply='{"ok":true,"interrupted":true}')
    types = [q["type"] for q, _, _ in posts]
    assert types == ["init", "events"]  # nothing after the stop, not even "done"


def test_streamer_sends_the_token_of_a_visualizer_on_this_machine(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    log = tmp_path / "posts.log"
    sink = subprocess.Popen([sys.executable, "-c", SINK, str(log), "{}"],
                            stdout=subprocess.PIPE, text=True)
    try:
        port = int(sink.stdout.readline())
        token_file = tmp_path / "cache" / "ramulator" / f"visualizer-{port}.token"
        token_file.parent.mkdir(parents=True)
        token_file.write_text("s3cret\n")
        run_ddr5(ramulator.controller_plugin.LiveTraceStreamer(port=port))
    finally:
        sink.kill()
        sink.wait()
    auths = {json.loads(line)["auth"] for line in log.read_text().splitlines()}
    assert auths == {"Bearer s3cret"}


def test_streamer_gives_up_on_a_visualizer_that_does_not_answer():
    # Connections are accepted (listen backlog) but never answered.
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        server.listen(16)
        start = time.monotonic()
        run_ddr5(ramulator.controller_plugin.LiveTraceStreamer(port=server.getsockname()[1]))
        assert time.monotonic() - start < 30
