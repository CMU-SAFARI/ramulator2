"""Collect the Ayna plugin's results from ``Simulation.stats``."""

from dataclasses import dataclass


@dataclass(frozen=True)
class ChannelPower:
    """One controller's DRAM power over the stats window."""

    channel: str
    energy_pJ: float
    avg_power_mW: float
    cycles: int
    domain_energy_pJ: list
    breakdown_pJ: dict
    bytes_transferred: int

    @property
    def pJ_per_bit(self):
        bits = self.bytes_transferred * 8
        return self.energy_pJ / bits if bits else float("nan")


_BREAKDOWN = [
    "E_act",
    "E_pre",
    "E_bg_act",
    "E_bg_pre",
    "E_RD",
    "E_WR",
    "E_RDA",
    "E_WRA",
    "E_pre_RDA",
    "E_pre_WRA",
    "E_ref_AB",
]


def _as_list(node):
    if node is None:
        return []
    return node if isinstance(node, list) else [node]


def summarize(stats):
    """Per-channel power from ``sim.stats`` (one entry per controller with an Ayna plugin)."""
    channels = []
    for ctrl in _as_list(stats.get("memory_system", stats).get("controller")):
        for plugin in _as_list(ctrl.get("controller_plugin")):
            if plugin.get("impl") != "Ayna":
                continue
            served = ctrl.get("num_read_reqs_served", 0) + ctrl.get("num_write_reqs_served", 0)
            channels.append(
                ChannelPower(
                    channel=ctrl.get("id", "Channel 0"),
                    energy_pJ=plugin["E_total"],
                    avg_power_mW=plugin["avg_power_mW"],
                    cycles=plugin["cycles"],
                    domain_energy_pJ=list(plugin["domain_E_total"]),
                    breakdown_pJ={k: plugin[k] for k in _BREAKDOWN},
                    bytes_transferred=served * plugin["tx_bytes"],
                )
            )
    return channels
