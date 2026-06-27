#ifndef RAMULATOR_BASE_PARAM_H
#define RAMULATOR_BASE_PARAM_H

#include <optional>
#include <stdexcept>
#include <string>

#include "ramulator/base/config_node.h"

namespace Ramulator {

template <typename T>
struct ParamReader {
  const ConfigNode& config;
  std::string name;
  std::string impl_name;

  T required() const {
    if (!config[name]) {
      throw std::runtime_error("Param \"" + name + "\" for \"" + impl_name + "\" is required but not given.");
    }
    return parse();
  }
  T default_val(T d) const {
    return config[name] ? parse() : d;
  }
  std::optional<T> optional() const {
    return config[name] ? std::optional<T>(parse()) : std::nullopt;
  }
  operator T() const {
    return required();
  }

 private:
  T parse() const {
    try {
      return config[name].as<T>();
    } catch (const std::runtime_error& e) {
      throw std::runtime_error("Failed to parse param \"" + name + "\" for \"" + impl_name + "\": " + e.what());
    }
  }
};

}  // namespace Ramulator

// Param declaration macro — both executes param parsing AND serves as codegen marker.
// Chain .required() or .default_val(X) after the macro call.
// Codegen parses these to auto-generate Python component wrappers.
//
// Usage:
//   RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
//   RAMULATOR_PARSE_PARAM(m_ipc, int, "ipc").default_val(4);
#define RAMULATOR_PARSE_PARAM(var, type, name) \
  var = Ramulator::ParamReader<type> {         \
    m_config, name, m_name                     \
  }

#endif  // RAMULATOR_BASE_PARAM_H
