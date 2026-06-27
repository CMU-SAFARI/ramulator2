"""Per-standard dictionaries for latency-throughput tests.

Drop a new file (e.g. ddr6.py) in this directory that defines a
module-level `config = {...}` and it will be picked up automatically.
Files starting with `_` are ignored.
"""

import importlib
import pkgutil

STANDARDS = {}
for module_info in pkgutil.iter_modules(__path__):
    if module_info.name.startswith("_"):
        continue
    mod = importlib.import_module(f".{module_info.name}", __package__)
    if not hasattr(mod, "config"):
        raise ImportError(
            f"tests/latency_throughput/testcases/{module_info.name}.py: "
            f"must define module-level `config = {{...}}`"
        )
    config = mod.config
    if not isinstance(config, dict) or "name" not in config:
        raise ImportError(
            f"tests/latency_throughput/testcases/{module_info.name}.py: "
            f"`config` must be a dict with a `name` field"
        )
    STANDARDS[config["name"]] = config
