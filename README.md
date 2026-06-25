# Ramulator 2.1 User Guide

- [1. Overview](#1-overview)
- [2. Using Ramulator 2.1](#2-using-ramulator-21)
- [3. Your First Run](#3-your-first-run)
- [4. Writing Configurations](#4-writing-configurations)
- [5. Validation and Regression Tests](#5-validation-and-regression-tests)
- [6. gem5 Integration](#6-gem5-integration)
- [7. Using Ramulator as a pure C++ Library](#7-using-ramulator-as-a-pure-c-library)
- [8. Extending Ramulator](#8-extending-ramulator)
- [9. How Ramulator Works Internally](#9-how-ramulator-works-internally)

## 1. Overview

### 1.1 Introduction

Ramulator 2.1 is a modern, modular, and extensible cycle-level DRAM simulator. It is the successor of Ramulator 1.0 [Kim+, CAL'16] and a major overhaul of Ramulator 2.0 [Luo+, CAL'23]. The goal of Ramulator 2.1 is to enable rapid and agile implementation and evaluation of design changes in the memory controller and DRAM to meet the increasing research effort in improving the performance, security, and reliability of memory systems. Ramulator 2.1 features a clean and modular C++ codebase with automatically generated Python wrappers that enables easy and scriptable configurations and extensions. Users can focus just on the C++ code that implements modeling and simulation logic without having to worry about manually maintaining boilerplate code.

Ramulator 2.1 can either be used as a standalone simulator that takes memory traces, or be easily integrated into other simulators as a DRAM and memory controller simulation library. We currently provide gem5 wrappers that works as a drop-in component for both SE and FS mode (tested with gem5 v25.1, FS mode tested with a post-boot checkpoint).

This Github repository contains the public version of Ramulator 2.1. From time to time, we will synchronize improvements of the code framework, additional functionalities, bug fixes, etc. from our internal version. Ramulator 2.1 welcomes your contribution as well as new ideas and implementations in the memory system.

Currently, Ramulator 2.1 provides the DRAM device and memory controller models for the following standards:
- DDR3, DDR4, DDR5
- GDDR6
- LPDDR5 (with WCK2CK sync and expiry tracking and tAAD deadline aware scheduling for separate ACT1 and ACT2)
- HBM1, HBM2, HBM3, HBM4 (Row and column command dual-issue and pseudochannels)

What has changed from Ramulator 2.0:
- Aggregated bug fixes
- More comprehensive support for newer DRAM & controller features
- More comprehensive sets of test and validation workflows
- Significantly improved the ease of use, configuration, and extension
- Overall code quality improvements

### 1.2 Repository Layout

- `src/`
  C++ code for main simulator implementation.
- `python/`
  Python wrappers for easy and scriptable configuration of Ramulator.
- `examples/`
  Ready-to-run example configurations and traces.
- `tests/`
  Tests and validation workflows.
- `resources/gem5_wrappers/`
  Reference wrapper code for gem5 integration.

## 2. Using Ramulator 2.1

We highly recommend to use our container (Dockerfile available at `.devcontainer/Dockerfile`) to avoid any dependency issues. The repository is also configured to be able to be one-click opened as a Dev Container with all the dependencies already installed. The easiest way to start using and developing Ramulator 2.1 is to directly create a Codespace on the GitHub page of the Ramulator 2.1 repository. If you are using Visual Studio Code, it should automatically prompt you to reopen the repository in a Dev Container after you clone and open the repo for the first time.

If you want to set up the container locally, you can do the following steps:
```bash
docker compose up -d --build
docker compose exec ramulator2 bash
```
Doing so creates a container with all the dependencies, mounts the Ramulator 2.1 repository at `/workspace`, and automatically activates `ramulator2-venv` in the container bash.

If you need to configure your own environment, please refer to Section 2.3 for detailed instructions. 

### 2.2 Getting Started

From the repository root:

```bash
mkdir -p build
cd build
cmake ..
make -j
cd ..
```

This default build does three useful things:

- Builds `libramulator.so` in the repository root
- Builds the Python extension module under `python/ramulator/`
- Runs the code generator so all the automatically generated code are in sync with the source code

Then run Ramulator 2.1 in standalone mode with an example configuration:

```bash
python3 examples/example_config.py
```

You should see some example statistics being printed. You can head to Section 3 directly for detailed explanations and instructions on how to use and configure Ramulator 2.1 if you do not need to build Ramulator 2.1 in your custom environment.

### 2.3 Build Requirements

**Required:**

- A C++20 compiler, such as `g++-12` or `clang++-15`
- CMake 3.14 or newer
- Python 3.10 or newer if you want the Python bindings, the CLI, or the tests

**Auto-fetched by CMake (no manual install):**

- `yaml-cpp` — YAML configuration parsing
- `fmt` — C++ print formatting
- `nanobind` — Python-C++ bindings (only when `RAMULATOR_PYTHON_BINDINGS=ON`)
- `argparse` — command-line argument parsing (vendored in `ext/`)

**Python dev dependencies** (install with `pip install -r requirements-dev.txt`):

- `pytest` — test framework
- `matplotlib` — latency-throughput plotting
- `ruff` — Python linter and formatter

`setuptools >= 64` is required as the build backend and is pulled in automatically by `pip install -e .`.

**Optional:** `clang-format`.

To build Ramulator 2.1 in standalone mode:
```bash
mkdir -p build
cd build
cmake ..
make -j
cd ..
```

If you only want the pure C++ library without Python bindings for your own simulator:

```bash
mkdir -p build
cd build
cmake .. -DRAMULATOR_PYTHON_BINDINGS=OFF
make -j
cd ..
```

### 2.4 Installing the Python Package

After building, install the Python package in editable mode so that `python -m ramulator` and `import ramulator` work from any directory:

```bash
pip install -e .
```

Then run examples directly:

```bash
python3 examples/example_config.py
```

If you prefer not to install the python package, you can also set `PYTHONPATH=python` as a one-off alternative:

```bash
PYTHONPATH=python python3 examples/example_config.py
```


## 3. Your First Run

### 3.1 Understanding Ramulator 2.1 Configurations

`examples/example_config.py` looks like the following:

```python
"""Example Ramulator2 configuration and simulation script"""

import ramulator

# Configure the simulation frontend that sends memory requests
frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)

# Create DRAM configuration
ddr4 = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
# Instantiate the memory controller with the DRAM configuration
ctrl = ramulator.controller.GenericDDR(
    dram=ddr4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
# Create a memory system with the controller
mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

# Run the simulation
sim = ramulator.Simulation(frontend, mem)
sim.run()

# sim.stats returns a nested Python dict of all simulation statistics
stats = sim.stats

# Guard here for `ramulator export`, which captures the configuration without
# running the simulation.
if stats:
    # Controller stats are under memory_system → controller
    ctrl_stats = stats["memory_system"]["controller"]

    print(f"Controller cycles:     {ctrl_stats['cycles']}")
    print(f"Avg read latency:      {ctrl_stats['avg_read_latency']:.1f} cycles")
    print(f"Read requests:         {ctrl_stats['num_read_reqs']}")
    print(f"Write requests:        {ctrl_stats['num_write_reqs']}")
    print(f"Row hits:              {ctrl_stats['row_hits']}")
    print(f"Row misses:            {ctrl_stats['row_misses']}")
    print(f"Row conflicts:         {ctrl_stats['row_conflicts']}")
```

There are two top-level components in Ramulator 2.1:

- `frontend`
  generates memory traffic. In this example it is a simple out-of-order core model driven by a memory instruction trace.
- `memory_system`
  encapsulates one or more memory controllers (channel). Each controller owns a DRAM device model.

Any Ramulator 2.1 simulation must contain these two components. They are used to create the main simulation object (`ramulator.Simulation(frontend, mem)`) used as the entry point of the simulation.

### 3.2 What Each Part Does

#### Frontend

```python
frontend = ramulator.frontend.SimpleO3(
    clock_ratio=8,
    traces=["./examples/traces/example_inst.trace"],
    num_expected_insts=500000,
    translation=ramulator.translation.NoTranslation(max_addr=2147483648),
)
```

The frontend generates memory requests and sends them to the memory system. In this example, `SimpleO3` models a simple Out-of-Order processor with an LLC. It reads one or more instruction trace files, each corresponds to a processor core. The simulation will run until the number of retired instructions has reached `500000`. The configured fontend will not apply address translation to the memory addresses in the trace (i.e., `ramulator.translation.NoTranslation`). `clock_ratio=8` means that for every `x` memory ticks (i.e., memory system side `clock_ratio=x`), the frontend will be ticked `8` times.

#### DRAM device

```python
ddr4 = ramulator.dram.DDR4(
    org_preset="DDR4_8Gb_x8",
    timing_preset="DDR4_2400R",
    rank=2,
)
```

This includes:

- An organization preset, such as die density, DQ width, number of banks, etc.
- A timing constraints preset, such as tRCD, tRAS, tRP, etc.
- Optional overrides to both presets. In this example, we set `rank=2`. You can append as many overrides as you want.

If you want to understand what this object turns into at runtime, section 9.3 walks through the full DRAM device model and the hierarchical state machine behind it.

#### Controller

```python
ctrl = ramulator.controller.GenericDDR(
    dram=ddr4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
```

This configures a GenericDDR memory controller for our just configured `ddr4` DRAM. It has an `FRFCFS` (First-Ready First-Come-First-Served) scheduler, an all-bank refresh, an `Open` row policy, and a `RoBaRaCoCh` address mapper.

#### Memory system

```python
mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)
```

`GenericDRAM` is a thin top-level wrapper around one or more controllers. It contains a `clock_ratio` that sets the memory-side tick rate, a list of controllers (`controllers=[...]`), and a channel mapper (`channel_mapper=...`) that decides which memory requests goes to which controller. `clock_ratio=3` means that for every `y` frontend ticks (i.e., front side `clock_ratio=y`), the memory system will be ticked `3` times. Currently, `GenericDRAM` requires all its memory controllers to have the same frequency.

### 3.4 What You Should Expect to See

The example prints a few key numbers from `sim.stats` after the simulation finishes:

```text
Controller cycles:     81302
Avg read latency:      45.2 cycles
Read requests:         6
Write requests:        0
Row hits:              3
Row misses:            3
Row conflicts:         0
```

The exact numbers depend on the workload and configuration.

`sim.stats` is a nested Python dict with two top-level keys: `"frontend"` and `"memory_system"`. The most useful counters live under `stats["memory_system"]["controller"]`:

| Stat | Meaning |
|------|---------|
| `cycles` | Controller cycles |
| `avg_read_latency` | Average read latency in controller cycles |
| `num_read_reqs` | Read requests accepted |
| `num_write_reqs` | Write requests accepted |
| `row_hits` | Total row hits |
| `row_misses` | Total row misses |
| `row_conflicts` | Total row conflicts |
| `read_queue_len_avg` | Average read queue occupancy |
| `write_queue_len_avg` | Average write queue occupancy |
| `total_num_read_requests` | Total reads accepted by the memory system (one level up, under `stats["memory_system"]`) |
| `total_num_write_requests` | Total writes accepted by the memory system |

With a single channel, `stats["memory_system"]["controller"]` is a dict. With multiple channels it becomes a list of dicts (one per channel), each with an `"id"` field (e.g., `"Channel 0"`).


## 4. Writing Configurations

### 4.1 The Main Components

The Ramulator Python package exposes the major components as a set of namespaces:

- `ramulator.frontend`
- `ramulator.dram`
- `ramulator.controller`
- `ramulator.scheduler`
- `ramulator.refresh_manager`
- `ramulator.row_policy`
- `ramulator.addr_mapper`
- `ramulator.channel_mapper`
- `ramulator.translation`
- `ramulator.controller_plugin`
- `ramulator.memory_system`

### 4.2 Common First Changes

#### Switch to another DRAM standard

You can swap DDR4 for another standard by replacing the DRAM object and, when needed, the controller class.

Examples:

```python
dram = ramulator.dram.DDR5(org_preset="DDR5_8Gb_x8", timing_preset="DDR5_4800AN")
ctrl = ramulator.controller.GenericDDR(dram=dram, ...)
```

```python
dram = ramulator.dram.LPDDR5(org_preset="LPDDR5_8Gb_x16", timing_preset="LPDDR5_5500")
ctrl = ramulator.controller.LPDDR5(dram=dram,...)
```

```python
dram = ramulator.dram.HBM2(org_preset="HBM2_2Gb", timing_preset="HBM2_2000Mbps")
ctrl = ramulator.controller.HBM12(dram=dram,...)
```

Use the controller that matches the standard you want to model. DDR3, DDR4, DDR5, and GDDR6 use `GenericDDR`. LPDDR5 uses `LPDDR5`. HBM1 and HBM2 use `HBM12`; HBM3 and HBM4 use `HBM34`.

#### Change rank count or other DRAM overrides

The DRAM object accepts preset names plus overrides:

```python
dram = ramulator.dram.DDR4(
    org_preset="DDR4_8Gb_x8",
    timing_preset="DDR4_2400R",
    rank=2, # This overrides the 1 rank settings in org_preset to be 2 ranks
    # Add more organization and timing overrides here 
)
```

Overrides are validated against the DRAM specification. Overriding non-existent parameters raise an error.

#### Add more channels

One controller corresponds to one channel.

```python
ctrl = ramulator.controller.GenericDDR(
    dram=ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=2),
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)

num_channels = 2
mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl] * num_channels, 
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)
```

One detail matters here: `CacheLineInterleave` currently requires the number of channels to be a power of two.

#### Choose a different frontend

The built-in frontends serve different purposes:

- `SimpleO3`
  Best first stop for memory-trace-driven studies with a simple core model and LLC. The memory trace includes both 1) the memory requests, and 2) the interval (i.e., the number of non-memory instructions) between consecutive memory requests. Please check `src/ramulator/frontend/impl/processor/simpleO3/simpleO3.cpp` for the trace format.
- `LoadStoreTrace`
  Replays a flat-address trace with `LD` and `ST` records. Intervals between memory requests are not modeled (i.e., memory requests are sent to the memory system on every cycle).
- `ReadWriteTrace`
  Replays a trace with `R` and `W` records. Similar to `LoadStoreTrace` but expects the address vector instead of flat-addresses. Good for debugging/testing.
- `LatencyThroughputTrace`
  Synthetic load generator used by the validation workflow that generates two kinds of memory requests: 1) random-access pointer-chasing like requests that are used to probe the memory access latency, and 2) streaming-access requests that generates load (configurable via the interval between consecutive streaming requests) on the memory system.

### 4.5 `sim.stats` and `sim.stats_yaml`

`sim.stats` returns all simulation statistics as a nested Python dict. This is the easiest way to access results that enables you to streamline your experiment workflow (configure, parameter sweep, result analyses) all in a single Python script. `sim.stats_yaml` returns the same data as a YAML-formatted string in case you want to save the results to disk.

## 5. Validation and Regression Tests

Ramulator includes four practical test layers under `tests/`.

### 5.1 How the Four Test Suites Differ

- `smoke`
  The fastest end-to-end sanity check. It verifies that each supported standard can build a realistic configuration and run without crashing.
- `latency_throughput`
  A modeling-fidelity suite built around generated frontend traffic. It checks whether unloaded latency and sustained bandwidth line up with the theoretical behavior implied by the DRAM definition.
- `device_timings`
  A short-sequence device-level correctness suite. It checks DRAM command legality, prerequisites, and timing-gate behavior one command at a time.
- `controller_scheduling`
  A short-sequence controller-level correctness suite. It checks emitted command sequences, row-hit/miss/conflict behavior, and priority-buffer scheduling contracts.

These suites are complementary:

- `smoke` answers "does it run at all?"
- `latency_throughput` answers "does the high-level performance shape look right?"
- `device_timings` answers "does the DRAM timing and prerequisite behavior look right?"
- `controller_scheduling` answers "does the controller schedule and emit commands the right way?"

### 5.2 Smoke Tests

Smoke tests make sure each supported standard can run end to end without crashing.

```bash
PYTHONPATH=python pytest tests/smoke -q
```

This is the fastest confidence check after a build or a local code change.

### 5.3 Fast Latency-Throughput

Fast latency-throughput is the main modeling-fidelity check used in regular development. It should finish in just a few minutes.

```bash
PYTHONPATH=python pytest tests/latency_throughput/test_fast.py -v -s
```

It does three things for each DRAM standard:

- Runs a no-refresh latency-throughput sweep
- Checks unloaded latency against the timing formula
- Checks measured streaming throughput against the theoretical peak

It also writes annotated plots to:

```text
tests/latency_throughput/plots/fast/
```

If you only want one standard:

```bash
PYTHONPATH=python pytest tests/latency_throughput/test_fast.py -v -s -k DDR4
```

### 5.4 Full Latency-Throughput

Full latency-throughput is a longer refresh-enabled sweep. It uses the same
latency-throughput curve shape as the fast suite, but runs each point with the
full profile's request count:

```bash
PYTHONPATH=python pytest tests/latency_throughput/test_full.py -v -s
```

It writes plots to:

```text
tests/latency_throughput/plots/full/
```

### 5.5 Device Timings

Device timings is the device-level correctness suite for short DRAM command sequences.

```bash
PYTHONPATH=python pytest tests/device_timings -q
```

It focuses on cases that are too small and specific for a throughput sweep:

- DRAM prerequisites such as `RD` requiring `ACT`
- timing gates such as `nRCD`, `nRTP`, and `nRP`

Use it when you change command semantics or device timing enforcement.

`DeviceUnderTest` gives you three complementary operations:

- `probe(command, addr_vec, clk)`
  A read-only query. It asks, "if I wanted this command at this cycle, what would the device say?" It does not change device state.
- `issue(command, addr_vec, clk)`
  A state-mutating operation. It actually issues the command into the device and updates timing/state. It throws if the command is not fully legal at that cycle.
- `assert_earliest_ready_at(command, addr_vec, clk)`
  A read-only assertion. It probes at `clk - 1` and asserts the command is *not* ready, then probes at `clk` and asserts it *is* ready. Use this to verify a timing gate is tight: legal as soon as possible, not earlier. The failure messages include `preq` and `timing_OK` so you can tell at a glance whether a regression is state-related or timing-related.

`probe()` returns five pieces of information:

- `preq`
  The command that should happen next from the device's point of view. If you probe `RD` on a closed bank, this will be `ACT`.
- `timing_OK`
  Whether timing alone allows the probed command at that cycle.
- `ready`
  Whether the command is fully issuable now. This is the combination of functional prerequisite and timing:
  `ready = (preq == command) and timing_OK`
- `row_hit`
  Whether the target bank is already open to the requested row.
- `row_open`
  Whether some row is already open in the target bank.

That distinction matters because a command can fail for two different reasons:

1. The bank state is wrong, so a different prerequisite command must happen first.
2. The bank state is right, but timing still blocks the command.

For example, a closed-bank read is not ready, but it can still be timing-OK:

```python
# A closed-bank read is functionally blocked until the row is opened.
closed = dut.probe("RD", a, clk=0)
# Without ACT issued, the prerequisite is ACT.
assert closed.preq == "ACT"
# Here we only check the timing constraints. The timing is OK here since no ACT has been issued yet!
assert closed.timing_OK is True
# ready means the command is fully issuable now: correct prerequisite and timing_OK.
assert closed.ready is False
```

Here, timing is not the problem. The missing prerequisite is.

After you issue `ACT`, the prerequisite changes to `RD`, but the access is still blocked until `nRCD` expires:

```python
dut.issue("ACT", a, clk=0)

early = dut.probe("RD", a, clk=dut.timings["nRCD"] - 1)
assert early.preq == "RD"
assert early.timing_OK is False
assert early.ready is False
```

Now the state is correct, but timing is not.

At exactly `nRCD`, the same probe becomes fully ready:

```python
ontime = dut.probe("RD", a, clk=dut.timings["nRCD"])
assert ontime.preq == "RD"
assert ontime.timing_OK is True
assert ontime.ready is True
```

That is the point where `issue()` becomes valid:

```python
dut.issue("RD", a, clk=dut.timings["nRCD"])
```

`issue()` is intentionally strict. It does not try to fix the sequence for you. If you call it on a command whose prerequisite is different, or on a command that is still timing-blocked, it raises an error. A good testing pattern is:

1. Use `probe()` to understand what the device expects next.
2. Assert on `preq`, `timing_OK`, and `ready`.
3. Call `issue()` only when you expect `ready` to be `True`.

A minimal `DeviceUnderTest` example looks like this:

```python
import ramulator
import tests.device_timings.harness as device_timings

dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
dut = device_timings.DeviceUnderTest(dram)
a = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=12, Column=0)

assert dut.probe("RD", a, clk=0).preq == "ACT"
dut.issue("ACT", a, clk=0)
assert dut.probe("RD", a, clk=dut.timings["nRCD"]).ready is True
```

The canonical full device example lives in:

- `tests/device_timings/example.py`

### 5.6 Controller Scheduling

Controller scheduling is the controller-level correctness suite for short request and maintenance sequences.

```bash
PYTHONPATH=python pytest tests/controller_scheduling -q
```

It focuses on cases that are too small and specific for a throughput sweep:

- controller-issued command sequences for row hits, misses, and conflicts
- scheduling preferences such as FRFCFS choosing a ready request
- controller contracts around priority/internal commands

Use it when you change row-policy behavior, controller scheduling, or command issuance logic.

A minimal `ControllerUnderTest` example looks like this:

```python
import ramulator
import tests.controller_scheduling.harness as cs

dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
dut = cs.ControllerUnderTest.make_generic_ddr(dram)

row0 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=0, Column=0)
row1 = dut.addr_vec(Rank=0, BankGroup=0, Bank=0, Row=1, Column=0)

dut.send_request("Read", row0)
dut.send_request("Read", row1)
history = dut.run_until_idle(max_ticks=128)
dut.assert_commands(["ACT", "RD", "PREpb", "ACT", "RD"], history=history)
```

The canonical full controller example lives in:

- `tests/controller_scheduling/examples/test_controller_example.py`


### 5.8 When to Use Which Test

- Use smoke tests after a clean build or a small code change
- Use fast latency-throughput when you change timing behavior, controller logic, or DRAM definitions
- Use device timings when you change command legality or timing enforcement
- Use controller scheduling when you change controller command sequencing or row-policy behavior
- Use full latency-throughput when you want a more comprehensive sanity check


## 6. gem5 Integration

Ramulator2 integrates with gem5 as a drop-in memory system. You can configure Ramulator2 in the same gem5 Python configuration script. The integration uses gem5's stdlib Board API, so it works with `SimpleBoard` and other stdlib boards.

Tested with gem5 v25.1.

### 6.1 Prerequisites

- gem5 built from source (stable branch recommended)
- Ramulator2 built with `libramulator.so` (the default build)
- Ramulator2 Python package available (either `pip install -e .` or `PYTHONPATH`)

### 6.2 Installing the Wrapper

Copy the wrapper files into your gem5 source tree:

```bash
cp -r resources/gem5_wrappers/ <gem5>/src/mem/ramulator2/
```

This creates four files:

| File | Purpose |
|------|---------|
| `Ramulator2.py` | gem5 SimObject declaration |
| `ramulator2.hh` | C++ header |
| `ramulator2.cc` | C++ implementation |
| `SConscript` | Build configuration |

Edit `SConscript` and set `RAMULATOR2_HOME` to your ramulator2 directory:

```python
# In <gem5>/src/mem/ramulator2/SConscript
RAMULATOR2_HOME = '/path/to/ramulator2'  # ← change this
```

Then rebuild gem5:

```bash
cd <gem5>
scons build/X86/gem5.opt -j$(nproc) --ignore-style
```

### 6.3 Writing a gem5 Config Script

A complete working example is in `examples/gem5_se_ramulator_hello_world.py`:

```python
import sys

# Replace with the actual path
sys.path.insert(0, "/path/to/ramulator2/python")

import ramulator
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

# ── Ramulator2 memory configuration ──

ddr4 = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
ctrl = ramulator.controller.GenericDDR(
    dram=ddr4,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
mem_sys = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

memory = ramulator.gem5.Memory(mem_sys, size="4GiB")

# ── gem5 system setup ──

processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=1)
board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=NoCache(),
)

board.set_se_binary_workload(binary=BinaryResource(local_path="/path/to/binary"))

simulator = Simulator(board=board)
simulator.run()
```

### 6.4 Running

```bash
cd <gem5>
build/X86/gem5.opt configs/your_config.py
```

The `SConscript` sets `RPATH` so `libramulator.so` is found automatically. If you moved the library after building, set `LD_LIBRARY_PATH` instead:

```bash
LD_LIBRARY_PATH=/path/to/ramulator2 build/X86/gem5.opt configs/your_config.py
```

For request-level debug tracing, add `--debug-flags=Ramulator2`.

Ramulator's internal stats (row hits, queue lengths, read latency, etc.) are written to `m5out/ramulator_stats.yaml` alongside gem5's own `stats.txt`.


## 7. Using Ramulator as a pure C++ Library

If you want to Ramulator in your simulator without introducing Python bindings for your simulator, you can still use Ramulator as a pure C++ library. 

### 7.1 The Usual Workflow

When embedding Ramulator inside another simulator, you use the `External` frontend. Your simulator sends memory requests to Ramulator and ticks the memory system; Ramulator handles scheduling, timing, and state tracking.

The typical flow is:

1. Build Ramulator (`libramulator.so`)
2. Write a Python config that uses `ramulator.frontend.External` and export it to YAML
3. Load that YAML from C++
4. Send memory requests via `receive_external_requests()`
5. Tick the memory system at the DRAM clock rate

Create a Python config script that uses the `External` frontend, then export it:

```python
import ramulator

frontend = ramulator.frontend.External(clock_ratio=1)

dram = ramulator.dram.DDR4(org_preset="DDR4_8Gb_x8", timing_preset="DDR4_2400R", rank=1)
ctrl = ramulator.controller.GenericDDR(
    dram=dram,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
mem = ramulator.memory_system.GenericDRAM(
    clock_ratio=3,
    controllers=[ctrl],
    channel_mapper=ramulator.channel_mapper.CacheLineInterleave(),
)

sim = ramulator.Simulation(frontend, mem)
```

Then run:

```bash
python3 -m ramulator export external_config.py -o config.yaml
```

The generated YAML is fully equivalent to the Python configuration that exports it from Ramulator's perspective. It is less readable than the Python configuration because it is intended for Ramulator to parse. You are not expected to manually edit the YAML files.

### 7.2 Minimal C++ Integration Skeleton

```cpp
#include <ramulator/base/config.h>
#include <ramulator/base/factory.h>
#include <ramulator/base/request.h>
#include <ramulator/frontend/i_frontend.h>
#include <ramulator/memory_system/i_memory_system.h>

// 1. Load config and create components
auto config = Ramulator::Config::parse_config_file("config.yaml");

auto* frontend = Ramulator::Factory::create_frontend(config);
auto* memory_system = Ramulator::Factory::create_memory_system(config);

frontend->connect_memory_system(memory_system);
memory_system->connect_frontend(frontend);

// 2. Send a read request
//    req_type_id: Request::Type::Read (0) or Request::Type::Write (1)
//    addr:        byte address
//    source_id:   identifies which core or source (0 for single-core)
//    callback:    called when the request completes
//    size_bytes:  request size in bytes
bool accepted = frontend->receive_external_requests(
    Ramulator::Request::Type::Read,   // read request
    0x1000,                           // address
    0,                                // source id
    [](Ramulator::Request& req) {
      // Request completed — req.depart has the completion cycle
    },
    64                                // size_bytes
);

// If accepted is false, the memory system's queue is full.
// Retry on the next cycle after ticking.

// 3. Tick the memory system each DRAM cycle
memory_system->tick();

// 4. When done, finalize to flush stats
frontend->finalize();
memory_system->finalize();
```

The `External` frontend's `tick()` is a no-op — your simulator controls when and how requests are injected. You only need to tick the memory system.

### 7.3 Integration Notes

- Exported configs are fully expanded. The C++ side expects resolved values, not symbolic Python presets.
- `libramulator.so` is the library you link against.
- `receive_external_requests()` returns `false` when the controller's request queue is full. The caller must retry on a subsequent cycle.
- `size_bytes` must be set for every external request and must not exceed the DRAM transaction size returned by `memory_system->get_tx_bytes()`.
- Request type IDs are `0` (read) and `1` (write), matching `Request::Type::Read` and `Request::Type::Write`.
- For a complete working integration, see the gem5 wrapper in `resources/gem5_wrappers/` or the gem5 integration section above.

## 8. Extending Ramulator

This section covers the extension points most contributors actually touch.

### 8.1 Adding a New Implementation

The most common change is adding a new implementation of an existing interface, such as a scheduler.

Create a single `.cpp` file:

```cpp
#include "controller/controller_base.h"
#include "controller/scheduler/i_scheduler.h"

namespace Ramulator {

// Inherit from both its interface and a common Implementation base class
class FooBarScheduler : public IScheduler, public Implementation {
  // Register the implementation class to the interface with a name ("FooBar")
  // Python wrappers are *automatically* generated
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FooBarScheduler, "FooBar")

  ControllerBase* m_ctrl = nullptr;
  int m_weight = 0;
  size_t s_decisions = 0;

  void init() override {
    // Initialize your component with parameters from the config
    RAMULATOR_PARSE_PARAM(m_weight, int, "weight").default_val(4);

    // If you need to access the parent component
    m_ctrl = cast_parent<ControllerBase>();

    // Register your variables as stats to be automatically printed 
    m_stats.add("foobar_decisions", s_decisions);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    // setup() gets called *after* all components have been initialized
    // You can resolve configurations that depends on other components here
    // ...
  }

  ReqBuffer::iterator get_best_request(
      ReqBuffer& buffer,
      RequestFilterRef filter) override {
        // Implement actual behavior and logic of the component by overriding interface virtual functions
  }
};

}  // namespace Ramulator
```

Then add that file to the relevant `CMakeLists.txt`, rebuild, and use it from Python:

```python
ctrl = ramulator.controller.GenericDDR(
    dram=dram,
    scheduler=ramulator.scheduler.FooBar(weight=8), # New scheduler!
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
)
```


### 8.2 Controller Plugins

Controller plugins are optional observer components attached to a controller.

Today, the plugin lifecycle is:

- `pre_schedule()`
  Runs before candidate selection
- `on_issue(const Request&)`
  Runs after a command is issued
- `post_schedule()`
  Runs at the end of the controller tick

Built-in plugins include:

- `CommandCounter`
  Counts selected DRAM commands and writes `command, count` lines to a CSV file
- `CmdTraceRecorder`
  Records every issued command to a per-channel trace file such as `trace.csv.ch0`
- `IssuedCommandValidationHook`
  Forwards each issued DRAM command to the controller scheduling test suite in Python

Example:

```python
ctrl = ramulator.controller.GenericDDR(
    dram=dram,
    scheduler=ramulator.scheduler.FRFCFS(),
    refresh_manager=ramulator.refresh_manager.AllBank(),
    row_policy=ramulator.row_policy.Open(),
    addr_mapper=ramulator.addr_mapper.RoBaRaCoCh(),
    controller_plugins=[
        ramulator.controller_plugin.CommandCounter(
            commands_to_count=["ACT", "PREpb", "RD", "WR", "REFab"],
            path="cmd_counts.csv",
        ),
    ],
)
```

### 8.3 Extending a DRAM Standard

DRAM standards are defined in Python scripts that describes the specifications of the DRAM standard. Ramulator automatically generates C++ code that fits the modeling methodology during the build process. Users can enjoy the readability,  flexibility and extensibility of Python and avoid low-level code that is much less readable.

For example, if you want to create a variant of an existing DRAM standard by adding a new DRAM command (e.g., `FOO`), follow the following two simple steps:

1. Add the implementation of the `FOO` command under `src/ramulator/dram/commands/`.

2. Create the variant DRAM standard by simply inheriting from the base DRAM standard and specify only what changes:

```python
import math

from ramulator.dram.ddr3 import DDR3
from ramulator.dram.spec import TimingConstraint


class DDR3Foo(DDR3):
    name = "DDR3Foo"

    # Add the new command
    commands = DDR3.commands + ["FOO"]

    # Add new timing constraints from the new command
    timing_params = DDR3.timing_params + ["nFOO"]
    timing_constraints = DDR3.timing_constraints + [
        TimingConstraint(level="Bank", preceding=["FOO"], following=["ACT"], latency="nFOO"),
        TimingConstraint(level="Bank", preceding=["ACT"], following=["FOO"], latency="nRC"),
    ]
```


### 8.4 Creating a New DRAM Standard

A new DRAM standard is a `DRAMStandard` subclass under `python/ramulator/dram/`.

You define:

- `name`
- `levels`
- `commands`
- `states`
- `timing_params`
- `supported_requests`
- `timing_constraints`
- `org_presets`
- `timing_presets`
- `resolve_secondary_timings()`

Code generation imports modules under `python/ramulator/dram/`, discovers these classes, and generates the corresponding C++ implementation in `src/ramulator/dram/impl/`.

### 8.5 Adding a New Standard to Latency-Throughput

After the DRAM definition exists, add a testcase file in `tests/latency_throughput/testcases/`.

The current latency-throughput flow expects a config shape like this:

```python
config = {
    "name": "MyStandard",
    "dram_class": "MyStandard",
    "org_preset": "MyOrgPreset",
    "timing_preset": "MyTimingPreset",
    "controller_class": "GenericDDR",
    "stream_cls": 8,
    "nop_counters": (1, 10, 100, 1000),
}
```

The exact controller class depends on the standard. For example, DDR3/DDR4/DDR5/GDDR6 use `GenericDDR`, LPDDR5 uses `LPDDR5`, HBM1/HBM2 use `HBM12`, and HBM3/HBM4 use `HBM34`.

Then run:

```bash
PYTHONPATH=python pytest tests/smoke -v -k MyStandard
PYTHONPATH=python pytest tests/latency_throughput/test_fast.py -v -s -k MyStandard
PYTHONPATH=python pytest tests/latency_throughput/test_full.py -v -s -k MyStandard
```

## 9. How Ramulator Works Internally

This section describes the implementation contracts behind the public configuration and extension APIs. If you only want to run experiments, you can stop earlier and come back when you need the deeper model.

### 9.1 Interface and Component Framework

Ramulator uses an interface and implementation pattern. An interface models a type of component in the simulated system. The interface class defines the high-level protocol and contract that the component exposes to other components in the system (i.e., how should other components interact with this component) through virtual functions. Interface classes start with `I`, and the header files are prefixed with `i_`.

Examples:

- `IFrontEnd`
- `IMemorySystem`
- `IController`
- `IScheduler`
- `IRowPolicy`
- `IRefreshManager`
- `IAddrMapper`
- `IChannelMapper`
- `IControllerPlugin`
- `ITranslation`

Implementations are concrete instances of the component type that its interface models. Implementation classes define concrete behavior of the component it models by overriding the virtual functions of the interface classes. In Ramulator2, all implementation classes must inherit from both its interface class and a common `Implementation` class that provides basic shared utilities and boilerplate.


Ramulator2 implements a self-registrying component factory so that it can create the component hierarchy from the configuration automatically. Users do not need to worry about handling the factory boilerplate as long as they make their custom interfaces and implementations discoverable through the following one-line macros:

```cpp
RAMULATOR_REGISTER_INTERFACE(IfceClassName, "ifce_name")
RAMULATOR_REGISTER_IMPLEMENTATION(IfceClassName, ImplClassName, "ImplName")
```

The factory uses those registrations to automatically discover and create components from config data (e.g., the above example will be come `ramulator.ifce_name.ImplName` at the Python side). There is no hand-maintained registry file.

Similarily, by using the provided macros to parse parameters and create child components, the boilerplate codes for Python to discover them are also automatically generated. No manual maintenance of C++ to Python binding is necessary.
```cpp
RAMULATOR_PARSE_PARAM(parsed_variable, type_t, "param_name")
RAMULATOR_CREATE_CHILD(IfceClassName, "ifce_name")
```



### 9.2 Config Flow

In Python mode, the configuration path is:

1. You create Python component objects.
2. Each object serializes itself with `to_config()`.
3. The Python binding converts that nested dictionary into `ConfigNode`.
4. The C++ factory creates the top-level frontend and memory-system objects.
5. Child components are created recursively during `init()`.
6. The simulation loop advances frontend and memory system according to their clock ratios.

In C++ library mode, the only difference is that you load the `ConfigNode` tree from an exported YAML file instead of starting from Python objects.

### 9.3 DRAM Device Model And Hierarchical State Machine

The controller owns a `DRAMDevice`, and that device is where Ramulator turns a DRAM standard description into a live protocol model. The easiest way to think about it is that the device keeps two views of the same channel at the same time:

- A hierarchy of nodes for timing
- A flat bank-oriented view for command semantics

DRAM timing rules are written at different scopes. Some live at the bank level, some at bank group or rank, and some at the channel or pseudo-channel level. Functional state is usually answered from the point of view of a specific bank. Ramulator uses the hierarchy for scoped timing rules and the flat bank view for direct bank-local command semantics.

The device does not keep its own free-running clock. The controller owns `m_clk` and passes the current cycle into every device query and every issued command. That is why `DRAMDevice::check_timing`, `DRAMDevice::get_preq_command`, and `DRAMDevice::issue_command` all take `clk` as an argument.

#### 9.3.1 Where The Model Comes From

The model starts in Python, not in C++. Each DRAM standard is a `DRAMStandard` subclass under `python/ramulator/dram/`. That class defines:

- The hierarchy, through `levels`
- The legal command set, through `commands`
- The state names, through `states`
- The timing vocabulary, through `timing_params`
- The external request to DRAM-command mapping, through `supported_requests`
- The timing rules, through `TimingConstraint`
- Optional bus timing behavior, through `command_cycles`, `tick_multiplier`, `row_commands`, and `column_commands`

`to_config()` is where that Python definition becomes runtime data. It does more work than its name might suggest:

- Resolves the chosen organization and timing presets
- Applies user overrides such as `rank=2`
- Computes derived timings in `resolve_secondary_timings()`
- Evaluates timing expressions such as `nCL + nBL + 2 - nCWL`
- Scales everything into simulation ticks when `tick_multiplier` is greater than 1
- Expands each `TimingConstraint` into integer-indexed entries
- Adjusts latencies for multi-cycle commands using `command_cycles`
- Auto-generates bus occupancy constraints for standards that need them

By the time the config reaches C++, the symbolic DRAM description is already fully resolved. The C++ `DRAMSpec` does not re-derive JEDEC tables on the fly. It stores the resolved names, timing values, timing constraints, command metadata, bank-targeting mode, and function pointers for command behavior.

Runtime code works with resolved integer IDs, timing values, and command metadata instead of re-evaluating the symbolic Python definition on each tick.

#### 9.3.2 What Gets Instantiated At Runtime

When the controller initializes its device, `DRAMDevice::init()` does three things:

1. Takes ownership of the resolved `DRAMSpec`
2. Builds the root `DRAMNode`
3. Collects a flat list of all bank nodes

The node tree represents the structural hierarchy of one channel. For DDR4, that hierarchy is effectively:

```text
Channel -> Rank -> BankGroup -> Bank
```

For HBM3 and HBM4 it is:

```text
Channel -> PseudoChannel -> Sid -> BankGroup -> Bank
```

(`Sid` models the stack-die identifier introduced for high-stack HBM3/HBM4
parts. HBM1 omits both `PseudoChannel` and `Sid`; HBM2 has `PseudoChannel`
but no `Sid`.)

The tree stops before the `Row` level. Ramulator does not instantiate one node per physical row. Instead, it tracks row state lazily inside the bank-like node that owns those rows.

Each `DRAMNode` stores four kinds of state:

- `m_state`
  The coarse protocol state for that node, such as `Closed`, `Opened`, or LPDDR5's `Activating`
- `m_cmd_ready_clk`
  For each command, the earliest cycle when that command may next issue at this node
- `m_cmd_history`
  Recent issue times for each command, sized large enough to model the largest rolling window seen at that level
- `m_row_state`
  A map of currently open rows for that bank-like node

That last field is the reason Ramulator can model large devices without creating millions of row objects. Rows only appear in the state map if they have been opened. A closed bank has an empty `m_row_state`.

The flat bank array, `m_bank_nodes`, is just a different view of the same tree. It lets the controller ask bank-local questions without walking down the hierarchy every time.

#### 9.3.3 Querying the DRAMSpec

Components query `DRAMSpec` (defined in `src/ramulator/dram/dram_spec.h`) for level IDs, command IDs, and timing values. The API has two families:

**Existence checks** return `bool` — use these to test whether an optional feature is present before acting on it:

| Method | Returns |
|--------|---------|
| `has_level("X")` / `has_command("X")` / `has_state("X")` / `has_timing("X")` | `bool` |

**Value getters** return the integer ID or resolved value. If the name does not exist in the DRAM standard, they throw `std::runtime_error` immediately — there is no silent `-1` return:

| Method | Returns |
|--------|---------|
| `get_level_id("X")` / `get_command_id("X")` / `get_state_id("X")` | `int` — the integer ID for the given name |
| `get_timing_value("X")` | `int` — the resolved timing value (not an index) |
| `get_level_size("X")` | `int` — the number of instances at the named level (from `organization.level_sizes`) |

**Best practice: cache lookups at init-time.** The API does not prevent calling these at runtime, but string-keyed map lookups are unnecessary overhead on hot paths. The recommended pattern is to call them once in `init()` and store the results in member variables:

```cpp
void init() override {
  const auto& spec = *m_ctrl->m_device.m_spec;

  // Required lookups — throw immediately if the DRAM standard is missing these
  m_cmd_act  = spec.get_command_id("ACT");
  m_cmd_rd   = spec.get_command_id("RD");
  m_nCL      = spec.get_timing_value("nCL");
  m_bank_lvl = spec.get_level_id("Bank");

  // Optional features — check first, branch explicitly
  m_cmd_rda = spec.has_command("RDA") ? spec.get_command_id("RDA") : -1;
  m_has_ap  = (m_cmd_rda != -1);
}
```

At runtime, use the cached member variables (`m_cmd_act`, `m_nCL`, etc.) and direct array access (`spec.command_meta[cmd]`, `spec.bank_targets[cmd]`).

#### 9.3.4 The Timing Side: A Hierarchical Legality Check

The timing model is driven by `TimingConstraint`. Each constraint says:

- At which hierarchy level it applies
- Which preceding commands create the constraint
- Which following commands are blocked by it
- How long the latency is
- Whether a rolling history window is needed
- Whether the effect applies to sibling nodes rather than only the exact addressed path

At config time, those objects become `TimingConsEntry` records in `DRAMSpec::timing_cons`. From that point on, the runtime only deals with integer command IDs, level IDs, and resolved cycle counts.

Two methods on `DRAMNode` implement the timing algorithm:

- `DRAMNode::check_timing`
- `DRAMNode::update_timing`

`check_timing()` is the read-only timing side. It walks from the root toward the addressed scope and asks, at each node, whether the candidate command is still timing-blocked there. If the current cycle is earlier than `m_cmd_ready_clk[command]`, the answer is immediately false. Otherwise the walk continues.

If the address vector names a specific child, `check_timing()` follows that one path. If the address vector contains `-1` at the next level, the command is scoped broadly and timing must hold for every descendant in that scope. That is how commands such as `PREab` and `REFab` naturally become multi-bank checks without special-case traversal logic in the controller.

`update_timing()` is the write side. It runs when a command actually issues, and it updates both the targeted path and any relevant siblings.

The logic is easier to understand in two cases.

First, consider the node that lies on the addressed path. `update_timing()`:

- Records the issue time in `m_cmd_history[command]`
- Looks up all non-sibling timing constraints triggered by that command at this level
- Uses the recorded history to compute when each blocked command becomes legal again
- Updates `m_cmd_ready_clk` for those blocked commands
- Recurses into child nodes

Now consider a sibling node at the same level. If the address vector names a different child and the constraint entry is marked `sibling=true`, Ramulator updates that sibling's `m_cmd_ready_clk` without descending further. This is how rules that affect peer ranks or peer bank groups are modeled cleanly.

The `window` field is what makes rolling constraints work. A good example is `nFAW`, which limits how many activates can occur in a recent interval. If a constraint has `window=4`, the node keeps the four most recent issue times for that preceding command. When a new command arrives, Ramulator looks back to the fourth most recent one and uses that timestamp to decide when the next blocked command may issue.

Because the recursion visits every relevant level, timing rules compose naturally:

- Channel-level rules model shared buses or top-level serialization
- Rank-level rules model rank-wide interactions such as refresh and activate windows
- Bank-group rules model same-group restrictions
- Bank-level rules model per-bank open, close, and access timing
- Pseudo-channel rules model the per-PC timing domains used by HBM-family devices

The important takeaway is that Ramulator does not flatten all timing into one giant table. It keeps timing at the scope where the rule actually lives, then lets recursion combine those scopes at runtime.

#### 9.3.5 The State-Machine Side: What Command Should Happen Next

Timing tells you whether a command may issue now. It does not tell you which command should issue next for a request. That part comes from the command handlers registered in `populate_commands()`.

Each command may provide up to four bank-level handlers:

- `preq`
  Returns the prerequisite command that should be issued next
- `action`
  Mutates state when the command actually issues
- `rowhit`
  Answers whether the request is a row hit
- `rowopen`
  Answers whether some row is already open in the target bank


For a normal DDR-style access, the command chain is driven by `preq`:

- If the bank is closed, an access command's prerequisite is `ACT`
- If the bank is open to the requested row, the prerequisite is the access itself
- If the bank is open to the wrong row, the prerequisite becomes `PREpb`

That logic lives directly in the command handlers. `ACT::preq()` is a good example. It checks whether the bank is closed, already open to the same row, or open to a conflicting row, then returns `ACT`, the original command, or `PREpb` respectively.

`RD::preq()` and `WR::preq()` reuse that open-row logic instead of duplicating it. `PREpb::action()` closes the bank and clears `m_row_state`. `RDA` and `WRA` are modeled as access commands whose action also closes the bank. `REFab::preq()` checks whether all targeted banks are closed, and if not, it first requires `PREab`.

`BankTarget` determines how wide that bank-local dispatch is:

- `Single`
  One specific bank, used by commands such as `ACT`, `RD`, `WR`, and `PREpb`
- `All`
  Every bank in the addressed scope, used by commands such as `PREab` and `REFab`
- `SameBank`
  The same bank ID across a wider scope, used by standards that need that pattern, such as DDR5

`DRAMDevice::get_target_banks()` turns the address vector and `BankTarget` into concrete bank-node indices. That is why a refresh handler can still be written as bank-local logic while affecting many banks.

#### 9.3.6 A Concrete Request Walkthrough

It helps to walk through one ordinary read request on a closed DDR4 bank.

1. A frontend sends a read request into the controller.
2. The controller maps the physical address into `addr_vec`.
3. The controller sets `final_command` from the DRAM standard's `supported_requests`, so a read request targets `RD`.
4. The scheduler or controller calls `get_preq_command(final_command, addr_vec)`.
5. The device dispatches that question to the relevant bank node. Because the bank is closed, the answer is `ACT`.
6. The controller calls `check_timing(ACT, addr_vec)`. The hierarchy checks channel, rank, bank group, and bank timing state.
7. If timing allows it, `issue_command(ACT, addr_vec, clk)` runs. First it updates timing through the node tree, then it applies the functional action that changes the bank state to open and records the opened row.
8. Because `ACT` is marked as an opening command, the request moves to the active buffer instead of retiring.
9. On a later tick, the controller asks again for the prerequisite command. Now the bank is open to the right row, so the answer is `RD`.
10. `check_timing(RD, addr_vec)` validates the access against all relevant timing scopes.
11. `issue_command(RD, addr_vec, clk)` updates timing again. `RD` does not open a new row, so the request is now complete from the DRAM-command point of view.
12. The controller retires the request and assigns its departure time using the DRAM read latency.

If the bank had been open to the wrong row, the same flow would insert `PREpb` before `ACT`, and only then reach `RD`. The controller does not hardcode that sequence. It falls out of repeated prerequisite checks against current bank state.

A request is not expanded into a fixed command script ahead of time. Each cycle, the controller asks the device, "Given the state right now, what is the next legal command for this request?"


### 9.4 Controller Tick Flow

We explain the `GenericDDR` controller flow as an example:

1. `tick_prologue()`

   Advance the controller clock, accumulate queue-length statistics, and serve completed reads (i.e., calls their callback when they shall be returned to the frontend).

2. `m_refresh->tick()`

   Give the refresh manager a chance to inject maintenance work.

3. `m_rowpolicy->pre_schedule()` and plugin `pre_schedule()`

   Let policies react before candidate selection.
4. Candidate selection through 3 requests buffers (active, priority, and normal R/W)

   The controller tries to schedule active requests first, then priority requests, then normal read or write traffic. The active requests are the ones that already has their DRAM row open. Prioritizing them reduces premature precharges that wastes cycles.

5. `m_rowpolicy->try_upgrade_command(req)`

   Row policy may change a command in place, for example `RD` to `RDA` (i.e., close row policy), if that upgraded command is valid and ready.

6. Issue the command that the scheduled request needs to progress

   `m_device.issue_command(...)` updates timing state and any command-driven state changes.

7. Update stats and notify observers

   The controller updates row-hit and row-miss statistics, then calls `on_issue(...)` on the row policy and all plugins.

8. Advance the request lifecycle

   If the issued command is the final command, the request is retired. If it is an opening command such as `ACT`, the request is promoted to the active buffer.

9. `post_schedule()` hooks

   Row policy and plugins can do things at the end of the tick.

### 9.5 A Good Reading Order for the Source

If you want to understand the codebase without getting lost, this order works well:

1. `examples/example_config.py`
2. `python/ramulator/__init__.py`
3. `src/ramulator/python/bindings.cpp`
4. `src/ramulator/controller/impl/generic_ddr_controller.cpp`
5. `src/ramulator/controller/controller_base.cpp`
6. `src/ramulator/dram/device.h` and `src/ramulator/dram/node.cpp`
7. One DRAM definition in `python/ramulator/dram/`, such as `ddr4.py`

That path starts from the public API, then drops into the execution path, then finally into the deeper modeling machinery.
