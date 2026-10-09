"""DRAM power models for Ramulator.

The Ayna HBM power model of CMU-SAFARI/HBM-Power (HBM2, HBM3, HBM4) is a controller plugin:

    hbm3 = ramulator.dram.HBM3(org_preset="HBM3_16Gb_8hi", timing_preset="HBM3_6400Mbps")
    ayna = ramulator.controller_plugin.Ayna(power=ramulator.power.ayna_power())
    ctrl = ramulator.controller.HBM34(dram=hbm3, ..., controller_plugins=[ayna])
"""

from ramulator.power.ayna import ayna_power
from ramulator.power.data_pattern import DataPattern
from ramulator.power.export import ayna_config, ayna_trace
from ramulator.power.summary import ChannelPower, summarize

__all__ = ["ChannelPower", "DataPattern", "ayna_config", "ayna_power", "ayna_trace", "summarize"]
