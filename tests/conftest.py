"""Pytest configuration for Ramulator 2 tests."""


def pytest_addoption(parser):
    parser.addoption(
        "--verbose-plot",
        action="store_true",
        default=False,
        help="Include reference lines and metrics in latency-throughput plots",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "smoke: Tier 1 basic functional tests")
    config.addinivalue_line(
        "markers", "latency_throughput_fast: Fast no-refresh latency-throughput tests"
    )
    config.addinivalue_line(
        "markers", "latency_throughput_full: Full refresh-enabled latency-throughput tests"
    )
    config.addinivalue_line("markers", "device_timings: DRAM device timing and legality tests")
    config.addinivalue_line(
        "markers", "controller_scheduling: Controller request scheduling tests"
    )
