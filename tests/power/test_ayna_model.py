"""Self-contained tests of the Ayna power model: closed forms, characterization and plugin."""

import pytest

import ramulator
import ramulator.power
import tests.controller_scheduling.harness as cs
from tests.power.ayna_reference import Workload, run_workload

pytestmark = pytest.mark.power

# Ayna's device configurations (config/*/power.json), in V and mA.
HBM2_1200MTs = {
    "vDD": 1.2,
    "vDDQ": 1.2,
    "iDD0": 28.544,
    "iDD2N": 11.756,
    "iDD3N1": 12.394,
    "iDD3N16": 14.281,
    "iDD4R": 202.769,
    "iDD4W": 137.406,
    "iDD5B": 66.331,
}
HBM3E_6400MTs = {
    "vDD": 1.1,
    "vDDQ": 1.1,
    "iDD0": 106.1,
    "iDD2N": 70.5,
    "iDD3N1": 71.2,
    "iDD3N16": 73.3,
    "iDD4R": 560.7,
    "iDD4W": 389.5,
    "iDD5B": 164.7,
}
HBM4_8000MTs = {
    "vDD": 1.05,
    "vDDQ": 0.9,
    "iDD0": 128.9,
    "iDD2N": 88.1,
    "iDD3N1": 88.9,
    "iDD3N16": 91.0,
    "iDD4R": 700.9,
    "iDD4W": 486.9,
    "iDD5B": 192.5,
}


def _hbm3(org="HBM3_16Gb_8hi", **overrides):
    return ramulator.dram.HBM3(org_preset=org, timing_preset="HBM3_6400Mbps", **overrides)


def _ayna(**kwargs):
    return ramulator.controller_plugin.Ayna(power=ramulator.power.ayna_power(**kwargs))


def _ayna_stats(stats):
    plugins = stats["controller_plugin"]
    plugins = plugins if isinstance(plugins, list) else [plugins]
    return next(p for p in plugins if p["impl"] == "Ayna")


def _tick(dut, n):
    for _ in range(n):
        dut.tick()


def _electrical(dram, **kwargs):
    """The electrical characterization the plugin uses for ``dram``."""
    maker = (
        cs.ControllerUnderTest.make_hbm12
        if type(dram).name == "HBM2"
        else cs.ControllerUnderTest.make_hbm34
    )
    dut = maker(dram, controller_plugins=[_ayna(**kwargs)])
    p = _ayna_stats(dut.stats())
    return {k: p[k] for k in HBM3E_6400MTs}


# ── Characterizations ──────────────────────────────────────────────────


def test_currents_follow_the_speedbin(capfd):
    assert _electrical(_hbm3()) == HBM3E_6400MTs
    for timing in ("HBM4_8000Mbps", "HBM4_8000Mbps_folded_banks_baseline"):
        dram = ramulator.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset=timing)
        assert _electrical(dram) == HBM4_8000MTs, timing
    capfd.readouterr()
    hbm2 = ramulator.dram.HBM2(org_preset="HBM2_8Gb", timing_preset="HBM2_2000Mbps")
    assert _electrical(hbm2) == HBM2_1200MTs
    assert "characterized at 1200 MT/s; using them at 2000 MT/s" in capfd.readouterr().err


def test_rate_scaling_matches_ayna(capfd):
    # Ayna's array_at() and io_current() (sweep_hbm4_power.py) at 4.8 Gbps, less half of IDD2N.
    assert _electrical(_hbm3(rate=4800)) == {
        "vDD": 1.1,
        "vDDQ": 1.1,
        "iDD0": 83.3,
        "iDD2N": 52.8,
        "iDD3N1": 53.6,
        "iDD3N16": 55.7,
        "iDD4R": 420.5,
        "iDD4W": 292.1,
        "iDD5B": 137.0,
    }
    capfd.readouterr()
    assert _electrical(_hbm3(rate=9600))["iDD2N"] == 105.7
    assert "outside the 4800-8000 MT/s range" in capfd.readouterr().err


def test_options_and_overrides():
    power = ramulator.power.ayna_power(
        variation=True,
        data_pattern=ramulator.power.DataPattern.static(),
        iDD4R=210.0,
        floor_pJbit=3.0,
    )
    assert power["dataPattern"]["dq_rate"] == 0.0 and power["dataPattern"]["floor_pJbit"] == 3.0
    assert power["overrides"] == {"iDD4R": 210.0} and power["variation"]
    assert ramulator.power.ayna.STANDARDS["HBM2"]["iDD4R"] == 202.769  # not modified

    hbm2 = ramulator.dram.HBM2(org_preset="HBM2_8Gb", timing_preset="HBM2_2000Mbps")
    assert _electrical(hbm2, iDD4R=210.0) == {**HBM2_1200MTs, "iDD4R": 210.0}
    assert _electrical(_hbm3(), iDD2N=60.0, vDD=1.0) == {**HBM3E_6400MTs, "iDD2N": 60.0, "vDD": 1.0}
    with pytest.raises(RuntimeError, match="no measured variation factors for HBM3"):
        _electrical(_hbm3(), variation=True)
    with pytest.raises(ValueError, match="Unknown Ayna power field"):
        ramulator.power.ayna_power(iDD9=1.0)
    with pytest.raises(ValueError):
        ramulator.power.DataPattern(dq_rate=1.5)


def test_folded_banks_baseline_speedbin():
    base = ramulator.dram.HBM4(org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps").resolve()[
        1
    ]
    fb = ramulator.dram.HBM4(
        org_preset="HBM4_32Gb_8Hi", timing_preset="HBM4_8000Mbps_folded_banks_baseline"
    )
    t = fb.resolve()[1]
    paper_ns = {
        "nRCDRD": 16,
        "nRP": 16,
        "nRAS": 29,
        "nCL": 16,
        "nFAW": 16,
        "nRRDS": 2,
        "nRRDL": 2,
        "nCCDL": 2,
    }
    for name, ns in paper_ns.items():
        assert t[name] * t["tCK_ps"] >= ns * 1000 > (t[name] - 1) * t["tCK_ps"], name
    for name in ["nCWL", "nRCDWR", "nRTP", "nWR", "nWTRL", "nWTRS", "nRFC", "nREFI", "rate", "nBL"]:
        assert t[name] == base[name], name
    assert t["nRC"] == t["nRAS"] + t["nRP"]
    fb.to_config()


SPEEDBINS = [
    (standard, timing)
    for standard in ("HBM2", "HBM3", "HBM4")
    for timing in getattr(ramulator.dram, standard).timing_presets
]


@pytest.mark.parametrize("standard, timing", SPEEDBINS, ids=[t for _, t in SPEEDBINS])
def test_every_speedbin_and_organization_sets_up(standard, timing):
    std = getattr(ramulator.dram, standard)
    maker = (
        cs.ControllerUnderTest.make_hbm12
        if standard == "HBM2"
        else cs.ControllerUnderTest.make_hbm34
    )
    for org in std.org_presets:
        dram = std(org_preset=org, timing_preset=timing)
        try:
            dram.to_config()
        except ValueError as e:
            pytest.skip(f"Ramulator rejects {timing}: {e}")
        dut = maker(
            dram,
            refresh_manager=ramulator.refresh_manager.NoRefresh(),
            controller_plugins=[_ayna()],
        )
        _tick(dut, 1000)
        p = _ayna_stats(dut.stats())
        expected = p["vDD"] * p["iDD2N"] * 1e-3 * 1000 * dut.timing("tCK_ps")
        assert p["domain_E_total"] == pytest.approx(
            [expected] * len(p["domain_E_total"]), rel=1e-12
        ), org


# ── Closed forms ───────────────────────────────────────────────────────


def test_idle_pseudo_channels_draw_precharged_standby():
    for dram, maker in [
        (_hbm3(), cs.ControllerUnderTest.make_hbm34),
        (
            ramulator.dram.HBM2(org_preset="HBM2_8Gb", timing_preset="HBM2_2000Mbps"),
            cs.ControllerUnderTest.make_hbm12,
        ),
    ]:
        dut = maker(dram, controller_plugins=[_ayna()])
        _tick(dut, 1000)
        p = _ayna_stats(dut.stats())
        expected = p["vDD"] * p["iDD2N"] * 1e-3 * 1000 * dut.timing("tCK_ps")
        assert p["domain_E_total"] == pytest.approx(
            [expected] * len(p["domain_E_total"]), rel=1e-12
        )
        assert p["E_bg_pre"] == pytest.approx(p["E_total"], rel=1e-12)


def test_one_activation_matches_closed_form():
    dut = cs.ControllerUnderTest.make_hbm34(_hbm3(), controller_plugins=[_ayna()])
    dut.send_request(
        "Read",
        dut.addr_vec(Channel=0, PseudoChannel=0, Sid=1, BankGroup=0, Bank=0, Row=0, Column=0),
    )
    dut.run_until_idle()
    _tick(dut, 100)

    p, tCK = _ayna_stats(dut.stats()), dut.timing("tCK_ps")
    t_RAS, t_RP = dut.timing("nRAS") * tCK, dut.timing("nRP") * tCK
    IDD0, IDD2N, IDD3N1 = (p[k] * 1e-3 for k in ("iDD0", "iDD2N", "iDD3N1"))
    I_theta = (IDD0 * (t_RP + t_RAS) - IDD2N * t_RP) / t_RAS
    expected = p["vDD"] * (I_theta - IDD3N1) * t_RAS  # rho = 1, so I_1 = IDD3N1
    assert p["E_act"] == pytest.approx(expected, rel=1e-9)


def test_back_to_back_refresh_draws_idd5b():
    # Ayna's documented refresh property: REFA every tRFC draws exactly VDD * IDD5B.
    dut = cs.ControllerUnderTest.make_hbm34(_hbm3("HBM3_16Gb_4hi"), controller_plugins=[_ayna()])
    every_bank = dict(Sid=dut.ALL, BankGroup=dut.ALL, Bank=dut.ALL, Row=dut.ALL, Column=dut.ALL)
    for _ in range(4):
        dut.priority_send("REFab", dut.addr_vec(Channel=0, PseudoChannel=0, **every_bank))
    ticks, refs = 0, []
    while len(refs) < 4:
        refs += [c.clk for c in dut.tick() if c.command == "REFab"]
        ticks += 1
    nRFC = dut.timing("nRFC")
    assert [b - a for a, b in zip(refs, refs[1:])] == [nRFC] * 3  # back to back
    t0, end = refs[0], refs[-1] + nRFC  # a REFab completes on its issue tick
    _tick(dut, end - ticks)  # after k ticks the controller clock is k

    p = _ayna_stats(dut.stats())
    expected = p["vDD"] * 1e-3 * (p["iDD2N"] * t0 + p["iDD5B"] * (end - t0)) * dut.timing("tCK_ps")
    assert p["cycles"] == end
    assert p["domain_E_total"][0] == pytest.approx(expected, rel=1e-12)


def test_auto_precharge_closes_the_bank_at_aynas_time():
    dut = cs.ControllerUnderTest.make_hbm34(
        _hbm3(), row_policy=ramulator.row_policy.ClosedCAP(cap=1), controller_plugins=[_ayna()]
    )
    for column in (3, 4):  # ClosedCAP(cap=1) turns the second access to the row into RDA
        dut.send_request(
            "Read",
            dut.addr_vec(
                Channel=0, PseudoChannel=0, Sid=0, BankGroup=1, Bank=2, Row=7, Column=column
            ),
        )
    dut.run_until_idle()
    _tick(dut, 200)
    issued = {c.command: c.clk for c in dut.history}
    assert set(issued) == {"ACT", "RD", "RDA"}

    # Completion ticks: ACT is 3 ticks long, RDA 2 ticks.
    act, rda = issued["ACT"] + 2, issued["RDA"] + 1
    close = max(act + dut.timing("nRAS"), rda + dut.timing("nCL") + dut.timing("nBL"))
    stats = _ayna_stats(dut.stats())
    expected = (
        stats["vDD"] * stats["iDD3N1"] * 1e-3 * (close - act) * dut.timing("tCK_ps")
    )  # one bank open: IDD3N1
    assert stats["E_bg_act"] == pytest.approx(expected, rel=1e-9)
    assert stats["E_RDA"] > 0.0
    assert stats["E_pre"] == stats["E_pre_RDA"] == 0.0  # Ayna's HBM model: IBeta = IDD2N


def test_warmup_window_is_the_difference_of_two_snapshots(tmp_path):
    dram = _hbm3()
    full = Workload("full", requests=2000, tail_ticks=8000, seed=5)
    warm = Workload("warm", requests=2000, tail_ticks=8000, warmup_ticks=3000, seed=5)
    e_end = run_workload(dram, full, _mkdir(tmp_path / "a"))
    e_start = run_workload(dram, full, _mkdir(tmp_path / "b"), stop_at=3000)
    window = run_workload(dram, warm, _mkdir(tmp_path / "c"))
    assert window.end_tick == e_end.end_tick and e_start.end_tick == 3000
    for pc, energy in enumerate(window.stats["domain_E_total"]):
        expected = e_end.stats["domain_E_total"][pc] - e_start.stats["domain_E_total"][pc]
        assert energy == pytest.approx(expected, abs=1e-5)
    assert window.stats["cycles"] == e_end.end_tick - 3000


def _mkdir(path):
    path.mkdir()
    return str(path)


def test_unmodelled_commands_are_counted():
    dut = cs.ControllerUnderTest.make_hbm34(
        _hbm3(),
        refresh_manager=ramulator.refresh_manager.HBM34PerBankRefresh(),
        controller_plugins=[_ayna()],
    )
    _tick(dut, 20000)
    stats = _ayna_stats(dut.stats())
    assert stats["unmodelled_commands"] > 0
    assert stats["E_ref_AB"] == 0.0


def test_timing_that_breaks_the_model_is_rejected():
    # With nCL raised, an ACT may follow an RDA before Ayna's auto-precharge would close the bank.
    dram = _hbm3(nCL=60)
    with pytest.raises(RuntimeError, match="RDA -> ACT"):
        cs.ControllerUnderTest.make_hbm34(dram, controller_plugins=[_ayna()])


def _controller_stats_without_power(dram, plugins, workload):
    dut = cs.ControllerUnderTest.make_hbm34(
        dram, refresh_manager=ramulator.refresh_manager.AllBank(), controller_plugins=plugins
    )
    for i in range(workload):
        if i % 3 == 0:
            try:
                dut.send_request(
                    "Read" if i % 7 else "Write",
                    dut.addr_vec(
                        Channel=0,
                        PseudoChannel=i % 2,
                        Sid=(i // 2) % 2,
                        BankGroup=(i // 4) % 4,
                        Bank=(i // 16) % 4,
                        Row=(i * 7919) % 97,
                        Column=i % 32,
                    ),
                )
            except RuntimeError:
                pass
        dut.tick()
    _tick(dut, 15000)
    stats = dut.stats()
    stats.pop("controller_plugin", None)
    return stats, [(c.clk, c.command, tuple(c.addr_vec)) for c in dut.history]


def test_the_plugin_does_not_change_the_simulation():
    without = _controller_stats_without_power(_hbm3(), [], 6000)
    with_power = _controller_stats_without_power(_hbm3(), [_ayna()], 6000)
    assert with_power == without


def test_static_data_lowers_only_read_and_write_energy(tmp_path):
    dram = _hbm3()
    workload = Workload("rw", requests=1500, tail_ticks=2000, seed=6)
    random_data = run_workload(dram, workload, _mkdir(tmp_path / "r")).stats
    static_data = run_workload(
        dram, workload, _mkdir(tmp_path / "s"), data_pattern=ramulator.power.DataPattern.static()
    ).stats
    assert static_data["E_RD"] < random_data["E_RD"] and static_data["E_WR"] < random_data["E_WR"]
    for key in ["E_act", "E_pre", "E_bg_act", "E_bg_pre", "E_ref_AB"]:
        assert static_data[key] == random_data[key]


def test_summarize_full_simulation(tmp_path):
    trace = tmp_path / "mem.trace"
    trace.write_text(
        "".join(
            f"{'R' if i % 3 else 'W'} 0,{i % 2},{(i // 2) % 2},{(i // 4) % 4},{(i // 16) % 4},"
            f"{(i * 31) % 50},{i % 32}\n"
            for i in range(4000)
        )
    )
    ctrl = ramulator.controller.HBM34(
        dram=_hbm3(),
        scheduler=ramulator.scheduler.FRFCFS(),
        refresh_manager=ramulator.refresh_manager.AllBank(),
        row_policy=ramulator.row_policy.Open(),
        addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
        controller_plugins=[_ayna()],
    )
    mem = ramulator.memory_system.GenericDRAM(
        clock_ratio=1,
        controllers=[ctrl],
        channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
    )
    sim = ramulator.Simulation(
        ramulator.frontend.ReadWriteTrace(clock_ratio=1, path=str(trace)), mem
    )
    sim.run()
    sim.finalize()
    stats = sim.stats
    [channel] = ramulator.power.summarize(stats)
    plugin = _ayna_stats(stats["memory_system"]["controller"])
    assert channel.energy_pJ == plugin["E_total"] > 0
    assert sum(channel.domain_energy_pJ) == pytest.approx(channel.energy_pJ, rel=1e-9)
    assert channel.bytes_transferred > 0 and channel.pJ_per_bit > 0
