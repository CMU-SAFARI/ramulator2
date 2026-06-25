"""Code generator for Ramulator2.

Two generation directions:
  1. Python DRAM DSL → C++ headers (DRAMSpec subclasses with enums, static arrays)
  2. C++ component source → Python wrappers (params, children, __init__.py)

All package __init__.py files (including top-level and dram/) are auto-generated.

Usage:
    python -m ramulator codegen                  # generate all
    python -m ramulator codegen DDR4             # generate specific standard
    python -m ramulator codegen --dry-run        # print without writing
"""

import os
import sys

from ramulator.dram.spec import DRAMStandard

# Components with custom Python logic — skip auto-generation.
_CUSTOM_COMPONENTS = set()


def _discover_standards():
    """Import all known standard modules to trigger registration.

    Re-raises ImportError on broken spec modules with a clear message —
    the prior `except ImportError: pass` silently dropped the affected
    standard from the registry, so `codegen` printed "Generated …" for
    everything else and exited 0, leaving the broken standard absent
    from the build output with no warning.
    """
    import importlib
    import pkgutil

    import ramulator.dram as dram_pkg

    for _, name, _ in pkgutil.iter_modules(dram_pkg.__path__):
        if name in ("spec", "base", "__init__"):
            continue
        try:
            importlib.import_module(f"ramulator.dram.{name}")
        except ImportError as e:
            raise ImportError(
                f"codegen: failed to import ramulator.dram.{name} "
                f"during standard discovery — fix the import error in "
                f"that spec file or rename it out of the dram/ package. "
                f"Original error: {e}"
            ) from e

    return dict(DRAMStandard._registry)


def generate_header(cls):
    """Generate the C++ source file for a DRAMStandard subclass.

    Generates a class that inherits from DRAMSpec and self-populates
    in its constructor from static enums + config.
    """
    name = cls.name

    # Level enum
    level_names = list(cls.levels.keys())
    level_enum = ", ".join(level_names + ["COUNT"])

    # Command enum
    cmd_names = list(cls.commands)
    cmd_enum = ", ".join(cmd_names + ["COUNT"])

    # State enum
    state_names = list(cls.states)
    state_enum = ", ".join(state_names + ["COUNT"])

    # Timing enum — wrap at ~120 chars
    timing_enum_items = cls.timing_params + ["COUNT"]
    timing_lines = []
    current_line = "    "
    for i, item in enumerate(timing_enum_items):
        sep = ", " if i < len(timing_enum_items) - 1 else ""
        if len(current_line) + len(item) + len(sep) > 118:
            timing_lines.append(current_line.rstrip())
            current_line = "    "
        current_line += item + sep
    timing_lines.append(current_line.rstrip())
    timing_enum = "\n".join(timing_lines)

    # CommandImpls tuple
    cmd_impl_entries = [f"Cmd::{c}<{name}>" for c in cls.commands]
    impl_lines = []
    current_line = "      "
    for i, entry in enumerate(cmd_impl_entries):
        sep = ", " if i < len(cmd_impl_entries) - 1 else ""
        if len(current_line) + len(entry) + len(sep) > 118:
            impl_lines.append(current_line.rstrip())
            current_line = "      "
        current_line += entry + sep
    impl_lines.append(current_line.rstrip())
    cmd_impls = "\n".join(impl_lines)

    # init_states in constructor
    state_by_name = {s: f"State::{s}" for s in cls.states}
    init_state_entries = []
    for lv_name, init_state in cls.levels.items():
        init_state_entries.append(
            f"        {state_by_name[init_state]},{' ' * max(1, 14 - len(init_state))}// {lv_name}"
        )
    init_states = "\n".join(init_state_entries)

    # supported_requests in constructor
    req_entries = []
    for req_type, cmd_name in cls.supported_requests.items():
        req_entries.append(
            f"        Command::{cmd_name},{' ' * max(1, 10 - len(cmd_name))}"
            f"// {req_type} -> {cmd_name}"
        )
    supported_requests_body = "\n".join(req_entries)

    # Command include headers
    cmd_header_names = sorted(set(cls.commands))
    cmd_includes = "\n".join(f'#include "ramulator/dram/commands/{h}.h"' for h in cmd_header_names)

    # set_names call arguments
    level_name_entries = ", ".join(f'"{n}"' for n in level_names)
    cmd_name_entries = ", ".join(f'"{n}"' for n in cmd_names)
    state_name_entries = ", ".join(f'"{n}"' for n in cls.states)

    timing_name_items = cls.timing_params
    timing_name_lines = []
    current_line = "        "
    for i, item in enumerate(timing_name_items):
        sep = ", " if i < len(timing_name_items) - 1 else ""
        entry = f'"{item}"{sep}'
        if len(current_line) + len(entry) > 118:
            timing_name_lines.append(current_line.rstrip())
            current_line = "        "
        current_line += entry
    timing_name_lines.append(current_line.rstrip())
    timing_name_array = "\n".join(timing_name_lines)

    # Bus classification flags (for dual-bus standards like HBM)
    bus_lines = []
    if cls.row_commands:
        bus_lines.append("")
        bus_lines.append("      // Bus classification (for dual-bus controllers)")
        for c in cls.row_commands:
            bus_lines.append(f"      command_meta[Command::{c}].is_row_command = true;")
        for c in cls.column_commands:
            bus_lines.append(f"      command_meta[Command::{c}].is_column_command = true;")
    bus_flags = "\n".join(bus_lines) + "\n" if bus_lines else ""

    return f"""\
/******************************************************************************
 * AUTO-GENERATED FILE — DO NOT EDIT
 *
 * Generated by: python -m ramulator codegen
 * Source:       python/ramulator/dram/{name.lower()}.py
 *
 * Regenerate:   python -m ramulator codegen {name}
 ******************************************************************************/
#include "ramulator/dram/dram_spec.h"

#include "ramulator/dram/commands/populate.h"
{cmd_includes}

namespace Ramulator {{

class {name} : public DRAMSpec {{
 public:
  struct Level {{
    enum : int {{ {level_enum} }};
  }};
  struct Command {{
    enum : int {{ {cmd_enum} }};
  }};
  struct State {{
    enum : int {{ {state_enum} }};
  }};
  struct Timing {{
    enum : int {{
{timing_enum}
    }};
  }};

  using CommandImpls = std::tuple<
{cmd_impls}
  >;

  {name}(const ConfigNode& config) {{
    // Counts
    level_count = Level::COUNT;
    command_count = Command::COUNT;
    state_count = State::COUNT;
    timing_count = Timing::COUNT;

    // String name maps + reverse lookup vectors
    set_names(levels, level_names, {{{level_name_entries}}});
    set_names(commands, command_names, {{{cmd_name_entries}}});
    set_names(states, state_names, {{{state_name_entries}}});
    set_names(timings, timing_names, {{
{timing_name_array}
    }});

    // Static spec data
    internal_prefetch_size = {cls.internal_prefetch_size};
    init_states = {{
{init_states}
    }};
    supported_requests = {{
{supported_requests_body}
    }};

    // Runtime config (organization, timing values, timing constraints)
    load_config(config);

    // Command handlers (function pointers, metadata, bank targets)
    populate_commands(CommandImpls{{}}, *this);
{bus_flags}  }}
}};

// Self-registration
static bool _dram_{name.lower()} = DRAMSpec::register_standard(
    "{name}", [](const ConfigNode& config) {{ return std::make_unique<{name}>(config); }});

}}  // namespace Ramulator
"""


# ── C++ → Python component generation ──────────────────────────────────


def _class_to_module_name(class_name):
    """Convert CamelCase class name to snake_case module file name."""
    import re

    # Insert underscore before uppercase letters that follow lowercase
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", class_name)
    # Insert underscore before uppercase letters that follow uppercase+lowercase
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", s)
    return s.lower()


def _format_python_default(val, py_type):
    """Format a default value as a Python literal."""
    if isinstance(val, str):
        return repr(val)
    return repr(val)


def generate_component_file(impl_name, params, children, source_path=None, class_name=None):
    """Generate a Python component wrapper file."""
    from ramulator.cpp_parser import CPP_TYPE_MAP

    if class_name is None:
        class_name = impl_name

    lines = [
        "###############################################################################",
        "# AUTO-GENERATED FILE — DO NOT EDIT",
        "#",
        "# Generated by: python -m ramulator codegen",
    ]
    if source_path:
        lines.append(f"# Source:       {source_path}")
    lines += [
        "#",
        "# Regenerate:   python -m ramulator codegen",
        "###############################################################################",
    ]

    # Determine imports needed
    has_params = bool(params)
    single_children = [c for c in children if not c["is_list"]]
    list_children = [c for c in children if c["is_list"]]
    lines.append("from ramulator.components import Component")
    imports = []
    if has_params:
        imports.append("Param")
    if single_children:
        imports.append("Child")
    if list_children:
        imports.append("ChildList")
    if imports:
        lines.append(f"from ramulator.param import {', '.join(imports)}")

    lines.append("")
    lines.append("")
    lines.append(f"class {class_name}(Component):")
    lines.append(f'    impl = "{impl_name}"')

    for p in params:
        cpp_type = p["cpp_type"]
        type_info = CPP_TYPE_MAP.get(cpp_type, (int, None))
        py_type, cpp_override = type_info

        parts = [py_type.__name__]
        if p["required"]:
            parts.append("required=True")
        elif p["default_val"] is not None:
            parts.append(f"default={_format_python_default(p['default_val'], py_type)}")
        if cpp_override:
            parts.append(f'cpp_type="{cpp_override}"')

        lines.append(f"    {p['name']} = Param({', '.join(parts)})")

    for child in single_children:
        lines.append(f'    {child["config_key"]} = Child("{child["config_key"]}")')

    for child in list_children:
        attr_name = child["config_key"] + "s"
        lines.append(f'    {attr_name} = ChildList("{child["config_key"]}")')

    lines.append("")
    return "\n".join(lines)


def generate_init_file(subpackage, class_modules):
    """Generate __init__.py for a subpackage.

    class_modules: list of (module_name, [class_names])
    """
    lines = [
        "###############################################################################",
        "# AUTO-GENERATED FILE — DO NOT EDIT",
        "#",
        "# Generated by: python -m ramulator codegen",
        "# Regenerate:   python -m ramulator codegen",
        "###############################################################################",
    ]

    # Add generated imports
    for mod, classes in sorted(class_modules):
        names = ", ".join(classes)
        lines.append(f"from .{mod} import {names}")

    # Build __all__
    all_classes = []
    for _, classes in sorted(class_modules):
        all_classes.extend(classes)

    lines.append("")
    lines.append(f"__all__ = {sorted(all_classes)}")
    lines.append("")
    return "\n".join(lines)


def generate_dram_init(standards):
    """Generate dram/__init__.py from discovered DRAMStandard subclasses."""
    lines = [
        "###############################################################################",
        "# AUTO-GENERATED FILE — DO NOT EDIT",
        "#",
        "# Generated by: python -m ramulator codegen",
        "# Regenerate:   python -m ramulator codegen",
        "###############################################################################",
    ]
    all_names = []
    for name in sorted(standards):
        mod_name = name.lower()
        lines.append(f"from .{mod_name} import {name}")
        all_names.append(name)
    lines.append("")
    lines.append(f"__all__ = {all_names}")
    lines.append("")
    return "\n".join(lines)


def generate_top_level_init(config_keys):
    """Generate ramulator/__init__.py with interface-based namespace imports.

    config_keys: set/list of interface config keys (e.g., "controller", "scheduler").
    Config keys are already snake_case — used directly as subpackage names.
    """
    import_lines = []
    for config_key in sorted(config_keys):
        import_lines.append(f"from ramulator import {config_key}")
    # DRAM is special — not an interface, has its own init generation
    import_lines.insert(0, "from ramulator import dram")

    all_names = ["dram"] + sorted(config_keys) + ["gem5", "Simulation"]

    return f"""\
###############################################################################
# AUTO-GENERATED FILE — DO NOT EDIT
#
# Generated by: python -m ramulator codegen
# Regenerate:   python -m ramulator codegen
###############################################################################
\"\"\"Ramulator2 — cycle-accurate DRAM simulator with Python bindings.\"\"\"

from ramulator.components import Component

# Interface-based namespaces — each subpackage = one C++ interface
{chr(10).join(import_lines)}

# Hand-written integration modules
from ramulator import gem5


class Simulation:
    \"\"\"Run a Ramulator2 simulation from Python component objects or raw dicts.\"\"\"

    def __init__(self, frontend, memory_system):
        from ramulator._ramulator import Simulation as _CppSimulation

        fe_config = frontend.to_config() if isinstance(frontend, Component) else frontend
        ms_config = (
            memory_system.to_config() if isinstance(memory_system, Component) else memory_system
        )

        # Build the top-level config dict expected by C++
        config = {{
            "frontend": fe_config,
            "memory_system": ms_config,
        }}
        self._sim = _CppSimulation(config)

    def run(self):
        \"\"\"Run the simulation to completion.\"\"\"
        self._sim.run()

    @property
    def stats(self):
        \"\"\"Finalize and return stats as a nested dict.\"\"\"
        return self._sim.get_stats()

    @property
    def stats_yaml(self):
        \"\"\"Finalize and return stats as a YAML-formatted string.\"\"\"
        return self._sim.get_stats_yaml()


__all__ = {all_names}
"""


def generate_components(src_dir, python_dir, repo_root, dry_run=False):
    """Parse C++ source and generate Python component wrappers."""
    from ramulator.cpp_parser import (
        parse_interface_keys,
        parse_params_with_inheritance,
        parse_registrations,
    )

    interface_keys = parse_interface_keys(src_dir)
    registrations = parse_registrations(src_dir)

    # Group components by subpackage
    # subpackage -> [(impl_name, class_name, params, children, cpp_source)]
    by_subpackage = {}

    for impl_name, info in sorted(registrations.items()):
        if impl_name in _CUSTOM_COMPONENTS:
            continue

        ifce_class = info["interface_class"]
        config_key = interface_keys.get(ifce_class)
        if not config_key:
            continue
        subpackage = config_key

        params, children = parse_params_with_inheritance(
            info["filepath"], info.get("base_class"), src_dir, interface_keys
        )

        # Registered impl name IS the Python class name — no renaming
        class_name = impl_name

        by_subpackage.setdefault(subpackage, []).append(
            (impl_name, class_name, params, children, info["filepath"])
        )

    generated = {}

    for subpackage, components in sorted(by_subpackage.items()):
        pkg_dir = os.path.join(python_dir, subpackage)
        class_modules = []  # (module_name, [class_name])

        for impl_name, class_name, params, children, cpp_source in components:
            mod_name = _class_to_module_name(class_name)
            # Make source path relative to repo root
            rel_source = os.path.relpath(cpp_source, repo_root) if cpp_source else None
            content = generate_component_file(impl_name, params, children, rel_source, class_name)
            path = os.path.join(pkg_dir, f"{mod_name}.py")
            generated[path] = content
            class_modules.append((mod_name, [class_name]))

        # Generate __init__.py
        init_content = generate_init_file(subpackage, class_modules)
        init_path = os.path.join(pkg_dir, "__init__.py")
        generated[init_path] = init_content

    return generated


def codegen_main(args):
    """Entry point for the codegen subcommand."""
    import argparse

    parser = argparse.ArgumentParser(
        prog="python -m ramulator codegen",
        description="Generate C++ headers and Python wrappers.",
    )
    parser.add_argument(
        "standards",
        nargs="*",
        help="Standard names to generate (default: all registered)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print generated code without writing files",
    )
    parser.add_argument(
        "--src-dir",
        default=None,
        help="C++ source directory (default: auto-detect from repo root)",
    )
    opts = parser.parse_args(args)

    # Find repo root (look for CMakeLists.txt)
    repo_root = None
    src_dir = opts.src_dir
    if src_dir is None:
        d = os.path.dirname(os.path.abspath(__file__))
        while d != "/":
            if os.path.exists(os.path.join(d, "CMakeLists.txt")):
                repo_root = d
                src_dir = os.path.join(d, "src", "ramulator")
                break
            d = os.path.dirname(d)
        if src_dir is None:
            print("Error: cannot find repo root (no CMakeLists.txt found)", file=sys.stderr)
            sys.exit(1)
    else:
        repo_root = os.path.dirname(os.path.dirname(src_dir))

    python_dir = os.path.join(repo_root, "python", "ramulator")

    # ── DRAM standard generation (Python → C++) ──
    all_standards = _discover_standards()

    if opts.standards:
        standards = {}
        for name in opts.standards:
            if name not in all_standards:
                print(
                    f"Error: unknown standard '{name}'. "
                    f"Available: {', '.join(sorted(all_standards))}",
                    file=sys.stderr,
                )
                sys.exit(1)
            standards[name] = all_standards[name]
    else:
        standards = all_standards

    if not standards:
        print("No DRAM standards found.", file=sys.stderr)
        sys.exit(1)

    generated = {}

    # Per-standard headers
    for name, cls in sorted(standards.items()):
        cls.validate()
        header = generate_header(cls)
        path = os.path.join(src_dir, "dram", "impl", f"{name}.cpp")
        generated[path] = header

    # ── Component generation (C++ → Python) ──
    component_files = generate_components(src_dir, python_dir, repo_root, dry_run=opts.dry_run)
    generated.update(component_files)

    # ── DRAM __init__.py ──
    dram_init = generate_dram_init(all_standards)
    generated[os.path.join(python_dir, "dram", "__init__.py")] = dram_init

    # ── Top-level __init__.py (config keys auto-derived from C++ interface registrations) ──
    from ramulator.cpp_parser import parse_interface_keys as _parse_ifce_keys

    config_keys = set(_parse_ifce_keys(src_dir).values())
    top_init = generate_top_level_init(config_keys)
    generated[os.path.join(python_dir, "__init__.py")] = top_init

    # ── Write or print ──
    if opts.dry_run:
        for path, content in sorted(generated.items()):
            rel = os.path.relpath(path, repo_root)
            print(f"=== {rel} ===")
            print(content)
    else:
        for path, content in sorted(generated.items()):
            os.makedirs(os.path.dirname(path), exist_ok=True)
            existing = ""
            if os.path.exists(path):
                with open(path) as f:
                    existing = f.read()
            if existing != content:
                with open(path, "w") as f:
                    f.write(content)
                rel = os.path.relpath(path, repo_root)
                print(f"  wrote {rel}")
            else:
                rel = os.path.relpath(path, repo_root)
                print(f"  unchanged {rel}")

    print(f"\nGenerated {len(standards)} DRAM standard(s): {', '.join(sorted(standards))}")
    num_components = sum(1 for p in component_files if not p.endswith("__init__.py"))
    if num_components:
        print(f"Generated {num_components} Python component wrapper(s)")
