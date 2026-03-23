#include "ramulator/base/config.h"

#include <filesystem>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace Ramulator {

namespace fs = std::filesystem;

// ---- YAML::Node → ConfigNode conversion ----

static ConfigNode yaml_to_confignode(const YAML::Node& node) {
  if (!node.IsDefined() || node.IsNull()) {
    return ConfigNode();
  }
  if (node.IsScalar()) {
    return ConfigNode(node.as<std::string>());
  }
  if (node.IsSequence()) {
    ConfigNode cn;
    for (const auto& item : node) {
      cn.push_back(yaml_to_confignode(item));
    }
    return cn;
  }
  if (node.IsMap()) {
    ConfigNode cn(ConfigNode::Map{});
    for (const auto& kv : node) {
      cn.set(kv.first.as<std::string>(), yaml_to_confignode(kv.second));
    }
    return cn;
  }
  return ConfigNode();
}

// ---- Public API ----

ConfigNode Config::parse_config_file(const std::string& path_str) {
  fs::path path(path_str);
  if (!fs::exists(path)) {
    std::cerr << "Config file " << path_str << " does not exist!" << std::endl;
    std::exit(-1);
  }

  YAML::Node node = YAML::LoadFile(path);
  return yaml_to_confignode(node);
}

ConfigNode Config::parse_config_string(const std::string& yaml_text) {
  YAML::Node node = YAML::Load(yaml_text);
  return yaml_to_confignode(node);
}

}  // namespace Ramulator
