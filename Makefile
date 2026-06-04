# Ramulator 2.1 — build + GDDR7/GDDR6 experiment runner
#
# Run inside the ramulator-dev container (repo at /workspace/ramulator2.1).
#   make build              # configure + compile everything (model, py ext, codegen)
#   make help               # list all experiment targets
#   make gddr7-all          # run all GDDR7 experiments (tests + sims + fast lat-tp)
#
# Override on the command line if needed, e.g.:  make JOBS=8 build
#                                                 make PYTHON=python3 gddr7-example

JOBS    ?= 4
# Prefer the container venv python; fall back to python3 on a bare host.
PYTHON  := $(shell [ -x /opt/ramulator-venv/bin/python ] && echo /opt/ramulator-venv/bin/python || echo python3)
PYTEST  := $(PYTHON) -m pytest
# Tests/scripts resolve `import ramulator` from python/ without needing pip install.
export PYTHONPATH := python

# Common flags: -v verbose, -s show prints (lat-tp tests print BW/latency).
PYTEST_FLAGS ?= -v
LAT_FLAGS    ?= -v -s

.DEFAULT_GOAL := help

# ───────────────────────── Build ─────────────────────────

.PHONY: build rebuild codegen install clean

build:                       ## Configure + compile everything (first build)
	mkdir -p build
	cmake -S . -B build
	cmake --build build -j$(JOBS)

rebuild:                     ## Incremental recompile (after editing C++)
	cmake --build build -j$(JOBS)

codegen:                     ## Regenerate C++/Python from the DRAM Python DSL
	$(PYTHON) -m ramulator codegen

install:                     ## Editable pip install so `import ramulator` works anywhere
	$(PYTHON) -m pip install -e .

clean:                       ## Remove the build directory
	rm -rf build

# ───────────────────── GDDR7 simulations ─────────────────────

.PHONY: gddr7-example gddr7-4ch gddr7-channel-latency

gddr7-example:               ## Single 8-bit GDDR7 channel, SimpleO3 trace -> stats
	$(PYTHON) examples/gddr7.py

gddr7-4ch:                   ## Full 4-channel GDDR7 device -> per-channel stats
	$(PYTHON) examples/gddr7_4ch.py

gddr7-channel-latency:       ## Latency vs channel count (1/2/4) -> table + PNG
	$(PYTHON) examples/gddr7_channel_latency.py

# ─────────────── GDDR7 low-level verification (tests) ───────────────

.PHONY: gddr7-test-device gddr7-test-controller gddr7-test-multichannel gddr7-tests

gddr7-test-device:           ## Device command-legality + timing tests
	$(PYTEST) tests/device_timings/test_gddr7.py $(PYTEST_FLAGS)

gddr7-test-controller:       ## Controller scheduling (RCK modes, dual-bus, refresh)
	$(PYTEST) tests/controller_scheduling/test_gddr7.py $(PYTEST_FLAGS)

gddr7-test-multichannel:     ## 4-channel request-distribution tests
	$(PYTEST) tests/controller_scheduling/test_gddr7_multichannel.py $(PYTEST_FLAGS)

gddr7-tests: gddr7-test-device gddr7-test-controller gddr7-test-multichannel  ## All GDDR7 unit tests

# ─────────── GDDR7 high-level latency-throughput (-> plots/) ───────────
# Fast = no refresh (plots/fast/), Full = refresh enabled (plots/full/). ~minutes each.

.PHONY: gddr7-lat-fast gddr7-lat-full \
        gddr7-lat-pam3 gddr7-lat-nrz gddr7-lat-disabled \
        gddr7-lat-start-read gddr7-lat-start-rckstrt

gddr7-lat-fast:              ## All GDDR7 variants, no refresh -> plots/fast/
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7 $(LAT_FLAGS)

gddr7-lat-full:              ## All GDDR7 variants, refresh enabled -> plots/full/
	$(PYTEST) tests/latency_throughput/test_full.py -k GDDR7 $(LAT_FLAGS)

gddr7-lat-pam3:              ## PAM3 (BL16) lat-tp curve
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7_PAM3 $(LAT_FLAGS)

gddr7-lat-nrz:               ## NRZ (BL32) lat-tp curve
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7_NRZ $(LAT_FLAGS)

gddr7-lat-disabled:          ## RCK disabled mode lat-tp curve
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7_disabled $(LAT_FLAGS)

gddr7-lat-start-read:        ## RCK start-with-read (01B) lat-tp curve
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7_start_with_read $(LAT_FLAGS)

gddr7-lat-start-rckstrt:     ## RCK start-with-RCKSTRT (10B) lat-tp curve
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR7_start_with_rckstrt $(LAT_FLAGS)

# ───────────────────────── GDDR6 ─────────────────────────

.PHONY: gddr6-test-device gddr6-lat-fast gddr6-lat-full gddr6-all

gddr6-test-device:           ## GDDR6 device command-legality + timing tests
	$(PYTEST) tests/device_timings/test_gddr6.py $(PYTEST_FLAGS)

gddr6-lat-fast:              ## GDDR6 lat-tp, no refresh -> plots/fast/
	$(PYTEST) tests/latency_throughput/test_fast.py -k GDDR6 $(LAT_FLAGS)

gddr6-lat-full:              ## GDDR6 lat-tp, refresh enabled -> plots/full/
	$(PYTEST) tests/latency_throughput/test_full.py -k GDDR6 $(LAT_FLAGS)

gddr6-all: gddr6-test-device gddr6-lat-fast  ## GDDR6 tests + fast lat-tp

# ─────────────────────── Aggregate ───────────────────────

.PHONY: gddr7-sims gddr7-all all-experiments smoke

gddr7-sims: gddr7-example gddr7-4ch gddr7-channel-latency  ## All GDDR7 example sims

gddr7-all: gddr7-tests gddr7-sims gddr7-lat-fast           ## Everything GDDR7 (fast lat only)

smoke:                       ## Quick end-to-end sanity across all standards
	$(PYTEST) tests/smoke -q

all-experiments: gddr7-all gddr6-all                       ## Every GDDR7 + GDDR6 experiment

# ───────────────────────── Help ─────────────────────────

.PHONY: help
help:                        ## List all targets with descriptions
	@echo "Ramulator 2.1 — build & GDDR7/GDDR6 experiments"
	@echo "Usage: make <target>   (run inside the ramulator-dev container)"
	@echo
	@grep -hE '^[a-zA-Z0-9_-]+:.*##' $(MAKEFILE_LIST) \
		| sort | awk 'BEGIN{FS=":.*## "}{printf "  \033[36m%-26s\033[0m %s\n", $$1, $$2}'
