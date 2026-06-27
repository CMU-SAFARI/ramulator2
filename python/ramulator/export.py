"""Config export: capture and serialize Ramulator2 configs."""

import json
import sys


def capture_config(script_path):
    """Execute a config script and return the captured config dict.

    Monkey-patches Simulation to capture the config without creating
    C++ objects or running the simulation.
    """
    import runpy

    import ramulator

    captured = {}
    orig_init = ramulator.Simulation.__init__
    orig_run = ramulator.Simulation.run
    orig_stats = ramulator.Simulation.stats
    orig_stats_yaml = ramulator.Simulation.stats_yaml

    def _capture_init(self, frontend, memory_system):
        from ramulator.components import Component

        fe = frontend.to_config() if isinstance(frontend, Component) else frontend
        ms = memory_system.to_config() if isinstance(memory_system, Component) else memory_system
        captured["config"] = {"frontend": fe, "memory_system": ms}

    ramulator.Simulation.__init__ = _capture_init
    ramulator.Simulation.run = lambda self: None
    ramulator.Simulation.stats = {}
    ramulator.Simulation.stats_yaml = ""

    old_argv = sys.argv
    try:
        sys.argv = [script_path]
        runpy.run_path(script_path, run_name="__main__")
    finally:
        sys.argv = old_argv
        ramulator.Simulation.__init__ = orig_init
        ramulator.Simulation.run = orig_run
        ramulator.Simulation.stats = orig_stats
        ramulator.Simulation.stats_yaml = orig_stats_yaml

    if "config" not in captured:
        raise RuntimeError(f"No Simulation created in {script_path}")

    return captured["config"]


def _yaml_scalar(value):
    """Format a scalar value for YAML output."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return str(value)
    s = str(value)
    if not s or any(c in s for c in (":", "#", "{", "[", "]", "}", ",")):
        return f'"{s}"'
    if s in ("true", "false", "yes", "no", "null"):
        return f'"{s}"'
    try:
        float(s)
        return f'"{s}"'
    except ValueError:
        pass
    return s


def _yaml_flow(value):
    """Render a value in YAML flow style (inline brackets)."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, list):
        return "[" + ", ".join(_yaml_flow(v) for v in value) + "]"
    return _yaml_scalar(value)


def _is_scalar_list(data):
    """True if data is a list containing only scalars (no dicts or nested lists)."""
    return isinstance(data, list) and all(isinstance(v, (int, float, str, bool)) for v in data)


def _yaml_lines(data, indent=0):
    """Generate YAML lines from a config dict/list/scalar."""
    prefix = "  " * indent

    if isinstance(data, dict):
        for key, value in data.items():
            if isinstance(value, dict):
                yield f"{prefix}{key}:"
                yield from _yaml_lines(value, indent + 1)
            elif _is_scalar_list(value):
                items = ", ".join(_yaml_scalar(v) for v in value)
                yield f"{prefix}{key}: [{items}]"
            elif isinstance(value, list):
                yield f"{prefix}{key}:"
                yield from _yaml_lines(value, indent + 1)
            else:
                yield f"{prefix}{key}: {_yaml_scalar(value)}"

    elif isinstance(data, list):
        for item in data:
            if isinstance(item, dict):
                first = True
                for line in _yaml_lines(item, indent + 1):
                    if first:
                        # Replace leading indent with "- " for first line
                        stripped = line.lstrip()
                        yield f"{prefix}- {stripped}"
                        first = False
                    else:
                        yield line
            elif isinstance(item, list):
                yield f"{prefix}- {_yaml_flow(item)}"
            else:
                yield f"{prefix}- {_yaml_scalar(item)}"

    else:
        yield f"{prefix}{_yaml_scalar(data)}"


def dict_to_yaml(data):
    """Serialize a config dict to a YAML string."""
    return "\n".join(_yaml_lines(data)) + "\n"


def dict_to_json(data):
    """Serialize a config dict to a JSON string."""
    return json.dumps(data, indent=2) + "\n"
