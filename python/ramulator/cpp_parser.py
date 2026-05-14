"""Parse C++ source files to extract component registrations, params, and children.

Used by codegen to auto-generate Python component wrappers from C++ source.
"""

import os
import re

# C++ type -> (Python type, cpp_type override or None)
CPP_TYPE_MAP = {
    "int": (int, None),
    "unsigned int": (int, "unsigned int"),
    "float": (float, None),
    "bool": (bool, None),
    "std::string": (str, None),
    "std::vector<std::string>": (list, None),
    "std::vector<int>": (list, "std::vector<int>"),
    "uint64_t": (int, "uint64_t"),
    "Addr_t": (int, "Addr_t"),
}

# Regex patterns
_RE_REGISTER_IMPL = re.compile(
    r"RAMULATOR_REGISTER_IMPLEMENTATION(?:_DERIVED)?\s*\(\s*"
    r"(\w+)\s*,\s*"  # interface class (e.g., IFrontEnd)
    r"\w+\s*,\s*"  # C++ class name (ignored — impl name from string)
    r"(?:(\w+)\s*,\s*)?"  # optional: base class (DERIVED variant only) — now captured
    r'"(\w+)"'  # impl name string
)

_RE_REGISTER_IFCE = re.compile(
    r"RAMULATOR_REGISTER_INTERFACE\s*\(\s*"
    r"(\w+)\s*,\s*"  # interface class
    r'"(\w+)"'  # config key
)

_RE_PARSE_PARAM = re.compile(
    r"RAMULATOR_PARSE_PARAM\s*\(\s*"
    r"\w+\s*,\s*"  # variable name (ignored)
    r"(.+?)\s*,\s*"  # C++ type
    r'"(\w+)"\s*\)'  # param name string
    r"\s*\.\s*"  # dot
    r"(required|default_val)\s*"  # method
    r"\(([^)]*)\)"  # method arg (empty for required())
)

# Matches both 2-arg and 3-arg forms:
#   RAMULATOR_CREATE_CHILD(var, IFoo)
#   RAMULATOR_CREATE_CHILD(var, IFoo, "Default")
_RE_CREATE_CHILD = re.compile(
    r'RAMULATOR_CREATE_CHILD\s*\(\s*\w+\s*,\s*(\w+)(?:\s*,\s*"(\w+)")?\s*\)'
)

# Comment-based child marker for children not created via RAMULATOR_CREATE_CHILD
# (e.g., DRAM which uses DRAMSpec::create() directly)
# Format: // RAMULATOR_CHILD: ConfigKey
_RE_CHILD_MARKER = re.compile(r"//\s*RAMULATOR_CHILD:\s*(\w+)")

# List-of-children macros: RAMULATOR_CREATE_CHILD_LIST and RAMULATOR_CREATE_OPTIONAL_CHILD_LIST
_RE_CREATE_CHILD_LIST = re.compile(r"RAMULATOR_CREATE_CHILD_LIST\s*\(\s*\w+\s*,\s*(\w+)\s*\)")
_RE_CREATE_OPTIONAL_CHILD_LIST = re.compile(
    r"RAMULATOR_CREATE_OPTIONAL_CHILD_LIST\s*\(\s*\w+\s*,\s*(\w+)\s*\)"
)

# Find class definition and an ordinary public base, if present.
_RE_CLASS_DEF = re.compile(
    r"class\s+(\w+)\s*(?:final\s*)?(?:\s*:\s*public\s+(\w+)(?:\s*,[^{]+)?)?\s*\{"
)


def _scan_files(src_dir, extensions=(".cpp", ".h")):
    """Yield all source files under src_dir."""
    for root, _, files in os.walk(src_dir):
        for f in files:
            if any(f.endswith(ext) for ext in extensions):
                yield os.path.join(root, f)


def _find_class_sources(src_dir, class_name):
    """Find all source files (.cpp and .h) associated with a class.

    Searches for 'class ClassName' pattern in headers. When a .h is found,
    also includes the companion .cpp (same directory and base name).
    Returns list of filepaths.
    """
    sources = []
    for filepath in _scan_files(src_dir):
        with open(filepath) as f:
            content = f.read()
        for m in _RE_CLASS_DEF.finditer(content):
            if m.group(1) == class_name:
                sources.append(filepath)
                # If .h found, also look for companion .cpp
                if filepath.endswith(".h"):
                    cpp_path = filepath[:-2] + ".cpp"
                    if os.path.exists(cpp_path):
                        sources.append(cpp_path)
                break
    return sources


def _find_class_base(src_dir, class_name):
    """Find the first ordinary public base class for class_name, if any."""
    for filepath in _scan_files(src_dir):
        with open(filepath) as f:
            content = f.read()
        for m in _RE_CLASS_DEF.finditer(content):
            if m.group(1) == class_name:
                return m.group(2)
    return None


def parse_interface_keys(src_dir):
    """Scan for RAMULATOR_REGISTER_INTERFACE, return {interface_class: config_key}."""
    result = {}
    for filepath in _scan_files(src_dir, extensions=(".h",)):
        with open(filepath) as f:
            content = f.read()
        for m in _RE_REGISTER_IFCE.finditer(content):
            result[m.group(1)] = m.group(2)
    return result


def parse_registrations(src_dir):
    """Scan for RAMULATOR_REGISTER_IMPLEMENTATION.

    Returns {impl_name: {interface_class, filepath, base_class}}.
    """
    result = {}
    for filepath in _scan_files(src_dir):
        with open(filepath) as f:
            content = f.read()
        for m in _RE_REGISTER_IMPL.finditer(content):
            result[m.group(3)] = {
                "interface_class": m.group(1),
                "filepath": filepath,
                "base_class": m.group(2),  # None for non-DERIVED
            }
    return result


def parse_params(filepath):
    """Scan a file for RAMULATOR_PARSE_PARAM calls.

    Returns list of {name, cpp_type, required, default_val}.
    """
    with open(filepath) as f:
        content = f.read()
    params = []
    for m in _RE_PARSE_PARAM.finditer(content):
        cpp_type = m.group(1).strip()
        name = m.group(2)
        method = m.group(3)
        arg = m.group(4).strip()

        param = {
            "name": name,
            "cpp_type": cpp_type,
            "required": method == "required",
            "default_val": None,
        }
        if method == "default_val" and arg:
            param["default_val"] = _parse_default(arg, cpp_type)
        params.append(param)
    return params


def parse_children(filepath, interface_keys):
    """Scan a file for RAMULATOR_CREATE_CHILD, RAMULATOR_CREATE_CHILD_LIST,
    and RAMULATOR_CHILD comment markers.

    Returns list of dicts: [{"config_key": str, "is_list": bool}, ...].
    """
    with open(filepath) as f:
        content = f.read()
    children = []
    seen = set()
    # Single-child macro
    for m in _RE_CREATE_CHILD.finditer(content):
        ifce_class = m.group(1)
        config_key = interface_keys.get(ifce_class)
        if config_key and config_key not in seen:
            children.append({"config_key": config_key, "is_list": False})
            seen.add(config_key)
    # List-of-children macros (required and optional)
    for m in _RE_CREATE_CHILD_LIST.finditer(content):
        ifce_class = m.group(1)
        config_key = interface_keys.get(ifce_class)
        if config_key and config_key not in seen:
            children.append({"config_key": config_key, "is_list": True})
            seen.add(config_key)
    for m in _RE_CREATE_OPTIONAL_CHILD_LIST.finditer(content):
        ifce_class = m.group(1)
        config_key = interface_keys.get(ifce_class)
        if config_key and config_key not in seen:
            children.append({"config_key": config_key, "is_list": True})
            seen.add(config_key)
    # Comment-based children (for non-standard creation patterns)
    for m in _RE_CHILD_MARKER.finditer(content):
        config_key = m.group(1)
        if config_key not in seen:
            children.append({"config_key": config_key, "is_list": False})
            seen.add(config_key)
    return children


def _merge_params_and_children(params, children, next_params, next_children):
    existing_names = {p["name"] for p in params}
    for p in next_params:
        if p["name"] not in existing_names:
            params.append(p)
            existing_names.add(p["name"])

    existing_child_keys = {c["config_key"] for c in children}
    for c in next_children:
        if c["config_key"] not in existing_child_keys:
            children.append(c)
            existing_child_keys.add(c["config_key"])


def _parse_class_with_bases(class_name, src_dir, interface_keys, visited):
    """Parse params/children for class_name and ordinary C++ bases first."""
    if not class_name or class_name in visited:
        return [], []
    if class_name == "Implementation":
        return [], []
    visited.add(class_name)

    params = []
    children = []

    base_class = _find_class_base(src_dir, class_name)
    base_params, base_children = _parse_class_with_bases(
        base_class, src_dir, interface_keys, visited
    )
    _merge_params_and_children(params, children, base_params, base_children)

    for source in _find_class_sources(src_dir, class_name):
        _merge_params_and_children(
            params,
            children,
            parse_params(source),
            parse_children(source, interface_keys),
        )

    return params, children


def parse_params_with_inheritance(filepath, base_class, src_dir, interface_keys):
    """Parse params from concrete file and recursive ordinary C++ bases.

    Returns (params, children) with base class contributions merged first.
    """
    params, children = _parse_class_with_bases(base_class, src_dir, interface_keys, set())

    _merge_params_and_children(
        params,
        children,
        parse_params(filepath),
        parse_children(filepath, interface_keys),
    )

    return params, children


def _parse_default(val_str, cpp_type):
    """Parse a C++ default value literal to a Python value."""
    val_str = val_str.strip()
    # Remove suffixes
    if val_str.endswith("ULL") or val_str.endswith("ull"):
        val_str = val_str[:-3]
    elif val_str.endswith("f") and cpp_type == "float":
        val_str = val_str[:-1]
    # Bool literal
    if val_str in ("true", "false"):
        return val_str == "true"
    # String literal
    if val_str.startswith('"') and val_str.endswith('"'):
        return val_str[1:-1]
    # Numeric
    try:
        if "." in val_str:
            return float(val_str)
        return int(val_str)
    except ValueError:
        return val_str
