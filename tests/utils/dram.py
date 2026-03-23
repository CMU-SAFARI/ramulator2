"""Shared DRAM creation helper for test suites."""

import ramulator


def create_dram(cfg):
    """Instantiate a DRAM object from a testcase CONFIG dict."""
    dram_cls = getattr(ramulator.dram, cfg["dram_class"])
    return dram_cls(org_preset=cfg["org_preset"], timing_preset=cfg["timing_preset"], **cfg["dram_kwargs"])
