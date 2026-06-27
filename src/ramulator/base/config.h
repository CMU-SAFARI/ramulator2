#ifndef RAMULATOR_BASE_CONFIG_H
#define RAMULATOR_BASE_CONFIG_H

#include <string>

#include "ramulator/base/config_node.h"

namespace Ramulator {
namespace Config {

/**
 * @brief    Load a generated YAML configuration file.
 *
 * Expects fully-specified, machine-generated YAML (produced by
 * `python -m ramulator export`). No preset resolution, no includes,
 * no parameter overrides.
 *
 * @param    path           Path to the yaml file.
 * @return   ConfigNode     A ConfigNode tree containing all configurations.
 */
ConfigNode parse_config_file(const std::string& path);

/**
 * @brief    Parse a YAML/JSON configuration string.
 *
 * Accepts any valid YAML (including JSON, which is a YAML subset).
 * Used by the gem5 wrapper to receive config built in Python.
 */
ConfigNode parse_config_string(const std::string& yaml_text);

}  // namespace Config
}  // namespace Ramulator

#endif  // RAMULATOR_BASE_CONFIG_H
