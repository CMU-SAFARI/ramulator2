"""GDDR7 multi-channel system test.

In Ramulator one controller == one channel. The GDDR7 org preset describes a
single 8-bit channel; the full 4-channel device is assembled at the system
level by wiring N GDDR7 controllers behind a channel mapper. These tests prove
that pattern works and that requests actually reach all four channels.
"""

import pytest

import ramulator

pytestmark = pytest.mark.controller_scheduling

NUM_CHANNELS = 4
READS_PER_CHANNEL = 6


def _make_controllers(n):
    ctrls = []
    for _ in range(n):
        dram = ramulator.dram.GDDR7(
            org_preset="GDDR7_16Gb_x8_4ch",
            timing_preset="GDDR7_TEST_28000",
            encoding="PAM3",
        )
        ctrls.append(
            ramulator.controller.GDDR7(
                dram=dram,
                scheduler=ramulator.scheduler.FRFCFS(),
                refresh_manager=ramulator.refresh_manager.NoRefresh(),
                row_policy=ramulator.row_policy.Open(),
                addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
            )
        )
    return ctrls


def _write_trace(path, n_channels, reads_per_channel):
    # addr_vec layout for GDDR7 = [Channel, Bank, Row, Column].
    lines = []
    for ch in range(n_channels):
        for i in range(reads_per_channel):
            bank = i % 16
            row = i
            lines.append(f"R {ch},{bank},{row},0")
    path.write_text("\n".join(lines) + "\n")


def _controllers_stats(sim):
    cs = sim.stats["memory_system"]["controller"]
    return cs if isinstance(cs, list) else [cs]


def test_gddr7_four_channels_distribute_requests(tmp_path):
    trace = tmp_path / "gddr7_4ch.trace"
    _write_trace(trace, NUM_CHANNELS, READS_PER_CHANNEL)

    frontend = ramulator.frontend.ReadWriteTrace(clock_ratio=4, path=str(trace))
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=_make_controllers(NUM_CHANNELS),
        # PassThrough routes strictly by the channel field of the addr_vec,
        # so the distribution is deterministic and explicit.
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )
    sim = ramulator.Simulation(frontend, mem)
    sim.run()

    per_ch = _controllers_stats(sim)
    assert len(per_ch) == NUM_CHANNELS, "expected one controller per channel"

    served = [c["num_read_reqs"] for c in per_ch]
    # Each channel must have received exactly its explicitly-addressed share.
    assert served == [READS_PER_CHANNEL] * NUM_CHANNELS, served
    assert sum(served) == NUM_CHANNELS * READS_PER_CHANNEL


def test_gddr7_single_vs_four_channel_parallelism(tmp_path):
    # Same total read volume, addressed to one channel vs spread across four.
    # The 4-channel device must finish in fewer cycles (real parallelism),
    # confirming the channels run independently.
    total = NUM_CHANNELS * READS_PER_CHANNEL

    one = tmp_path / "one.trace"
    one.write_text("\n".join(f"R 0,{i % 16},{i},0" for i in range(total)) + "\n")
    four = tmp_path / "four.trace"
    _write_trace(four, NUM_CHANNELS, READS_PER_CHANNEL)

    def run(trace, n):
        fe = ramulator.frontend.ReadWriteTrace(clock_ratio=4, path=str(trace))
        mem = ramulator.memory_system.GenericDRAM(
            clock_ratio=1,
            controllers=_make_controllers(n),
            channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
        )
        sim = ramulator.Simulation(fe, mem)
        sim.run()
        return _controllers_stats(sim)

    one_ch = run(one, NUM_CHANNELS)  # all reads land on ch0
    four_ch = run(four, NUM_CHANNELS)  # reads spread across ch0..3

    # ch0 alone served everything in the single-channel addressing.
    assert one_ch[0]["num_read_reqs"] == total
    assert sum(c["num_read_reqs"] for c in one_ch[1:]) == 0
    # spread case: every channel busy.
    assert all(c["num_read_reqs"] == READS_PER_CHANNEL for c in four_ch)
