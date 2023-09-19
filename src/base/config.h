#ifndef RAMULATOR_BASE_CONFIG_H
#define RAMULATOR_BASE_CONFIG_H

#include <filesystem>
#include <iostream>
#include <regex>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"


namespace Ramulator {
namespace Config {

/**
 * @brief    Load and parse the YAML configuration file for the simulation.
 *
 * @param    path           Path to the yaml file.
 * @return   YAML::Node     A YAML node containing all configurations.
 */
YAML::Node parse_config_file(const std::string& path, const std::vector<std::string>& params);


namespace Details {

/**
 * @brief    Load the YAML file and set the current path to the parent path of the file.
 * 
 * @param    path_str       Path to the yaml file.
 * @return   YAML::Node 
 */
YAML::Node load_config_file(const std::string& path_str);

/**
 * @brief    Traverse the YAML document to load any included YAML files.
 *
 * @param    node           The current root node.
 * @param    verbose        Whether to print every node during the traversal.
 */
void resolve_included_configs(YAML::Node node);

/**
 * @brief    Override the config (add if non-existent) in the YAML file with the command line options.
 *
 * @param    config         Parsed YAML configurations.
 * @param    params         Command line option names and values.
 */
void override_configs(YAML::Node config, const std::vector<std::string>& params);

}    // namespace Details
}    // namespace Config


}        // namespace Ramulator

#endif   // RAMULATOR_BASE_CONFIG_H
