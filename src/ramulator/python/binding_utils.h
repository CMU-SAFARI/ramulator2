#ifndef RAMULATOR_PYTHON_BINDING_UTILS_H
#define RAMULATOR_PYTHON_BINDING_UTILS_H

#include <nanobind/nanobind.h>
#include <nanobind/stl/map.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <map>
#include <stdexcept>
#include <string>

#include "ramulator/base/config_node.h"
#include "ramulator/dram/dram_spec.h"

namespace nb = nanobind;

using namespace Ramulator;

// ---- Python dict/list/scalar -> ConfigNode ----

inline ConfigNode py_to_confignode(nb::handle obj) {
  if (nb::isinstance<nb::dict>(obj)) {
    ConfigNode::Map map;
    for (auto [key, val] : nb::cast<nb::dict>(obj)) {
      map[nb::cast<std::string>(key)] = py_to_confignode(val);
    }
    return ConfigNode(std::move(map));
  }
  if (nb::isinstance<nb::list>(obj)) {
    ConfigNode::Seq seq;
    for (auto item : nb::cast<nb::list>(obj)) {
      seq.push_back(py_to_confignode(item));
    }
    return ConfigNode(std::move(seq));
  }
  if (nb::isinstance<nb::bool_>(obj)) {
    return ConfigNode(nb::cast<bool>(obj));
  }
  if (nb::isinstance<nb::int_>(obj)) {
    return ConfigNode(nb::cast<long long>(obj));
  }
  if (nb::isinstance<nb::float_>(obj)) {
    return ConfigNode(nb::cast<double>(obj));
  }
  if (nb::isinstance<nb::str>(obj)) {
    return ConfigNode(nb::cast<std::string>(obj));
  }
  if (obj.is_none()) {
    return ConfigNode{};
  }

  throw std::runtime_error("py_to_confignode: unsupported Python type");
}

// ---- ConfigNode -> Python object ----

inline nb::object confignode_to_py(const ConfigNode& node) {
  if (node.is_null()) {
    return nb::none();
  }
  if (node.is_map()) {
    nb::dict d;
    for (const auto& [k, v] : node.map()) {
      d[nb::cast(k)] = confignode_to_py(v);
    }
    return d;
  }
  if (node.is_sequence()) {
    nb::list l;
    for (const auto& item : node.seq()) {
      l.append(confignode_to_py(item));
    }
    return l;
  }

  const auto& s = node.scalar();
  if (s == "true") {
    return nb::cast(true);
  }
  if (s == "false") {
    return nb::cast(false);
  }
  try {
    size_t pos;
    long long i = std::stoll(s, &pos);
    if (pos == s.size()) {
      return nb::cast(i);
    }
  } catch (...) {
  }
  try {
    size_t pos;
    double d = std::stod(s, &pos);
    if (pos == s.size()) {
      return nb::cast(d);
    }
  } catch (...) {
  }
  return nb::cast(s);
}

// ---- Helpers ----

inline ConfigNode wrap_interface_config(const std::string& ifce_name, ConfigNode config) {
  return ConfigNode(ConfigNode::Map{{ifce_name, std::move(config)}});
}

inline ConfigNode inject_issued_command_validation_hook(ConfigNode controller_config) {
  ConfigNode plugins = controller_config["controller_plugins"];
  if (plugins.is_null()) {
    plugins = ConfigNode::Seq{};
  }
  plugins.push_back(ConfigNode::Map{{"impl", ConfigNode("IssuedCommandValidationHook")}});
  controller_config.set("controller_plugins", plugins);
  return controller_config;
}

inline void validate_addr_vec_size(const DRAMSpec& spec, const AddrVec_t& addr_vec) {
  if (static_cast<int>(addr_vec.size()) != spec.level_count) {
    throw std::runtime_error("addr_vec size mismatch: expected " + std::to_string(spec.level_count) + ", got " +
                             std::to_string(addr_vec.size()));
  }
}

inline std::map<std::string, int> timing_map(const DRAMSpec& spec) {
  std::map<std::string, int> out;
  for (int i = 0; i < spec.timing_count; i++) {
    out[spec.timing_names[i]] = spec.timing_vals[i];
  }
  return out;
}

#endif  // RAMULATOR_PYTHON_BINDING_UTILS_H
