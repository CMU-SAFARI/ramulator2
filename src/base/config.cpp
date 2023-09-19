#include "base/config.h"


namespace Ramulator {

namespace fs = std::filesystem;

YAML::Node Config::parse_config_file(const std::string& path_str, const std::vector<std::string>& params) {
  fs::path path(path_str);
  if (!fs::exists(path)) {
    spdlog::error("Config file {} does not exist!", path_str);
    std::exit(-1);
  }
  
  const fs::path curr_path(fs::current_path());
  YAML::Node node = YAML::LoadFile(path);
  Details::resolve_included_configs(node);
  Details::override_configs(node, params);
  fs::current_path(curr_path);
  return node;
}

YAML::Node Config::Details::load_config_file(const std::string& path_str) {
  fs::path path(path_str);
  if (!fs::exists(path)) {
    spdlog::error("Config file {} does not exist!", path_str);
    std::exit(-1);
  }

  YAML::Node node = YAML::LoadFile(path);
  fs::current_path(path.parent_path());
  return node;
}


void Config::Details::resolve_included_configs(YAML::Node node) {
  switch (node.Type()) {
    case YAML::NodeType::Scalar: {
      if (node.Tag() == "!include") {
        const fs::path curr_path = fs::current_path();
        node = load_config_file(node.as<std::string>());
        resolve_included_configs(node);
        fs::current_path(curr_path);
      }
      break;
    }

    case YAML::NodeType::Sequence: {
      for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        resolve_included_configs(*it);
      }
      break;
    }
    
    case YAML::NodeType::Map: {
      for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        resolve_included_configs(it->second);
      }
      break;
    }

    case YAML::NodeType::Null: [[fallthrough]];
    case YAML::NodeType::Undefined: break;
  }
}


void Config::Details::override_configs(YAML::Node config, const std::vector<std::string>& params) {
  // Get the key-value pairs from the command line options
  std::map<std::string, std::string> kv;

  for (const auto& param : params) {
    std::stringstream ss(param);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(ss, token, '=')) {
      tokens.push_back(std::move(token));
    }

    if (tokens.size() != 2 ) {
      spdlog::warn("Unrecognized parameter override {}. Ignoring it.", param);
    } else {
      kv[tokens[0]] = tokens[1];
    }
  }

  // Iterate over all kv pairs to change (create_component) the YAML nodes
  for (const auto& [key, value] : kv) {
    std::stringstream ss(key);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, '.')) {
      tokens.push_back(std::move(token));
    }

    // Go over the keys to locate (create_component) the node
    YAML::Node node;
    node.reset(config);
    for (const auto& token : tokens) {
      std::vector<uint> indices;

      std::regex match_brackets("\\[(\\d+)]");
      std::sregex_iterator it(token.begin(), token.end(), match_brackets);
      std::sregex_iterator end;

      while(it != end) {
        indices.push_back(std::stoi((*it)[1]));
        it++;
      }

      // We don't have array indices in this token
      if (indices.empty()) {
        if (!node[token]) {
          node[token] = YAML::Node(YAML::NodeType::Map);   
        }
        node.reset(node[token]);
      } else {
        if (indices.size() > 1) {
          spdlog::error("Nested sequence access is currently not supported!");
          std::exit(-1);
        }
        // Get the key of the map by removing the indices
        std::string _key = std::regex_replace(token, match_brackets, "");

        if (!node[_key]) {
          node[_key] = YAML::Node(YAML::NodeType::Sequence);
        } else if (node[_key].Type() != YAML::NodeType::Sequence) {
          spdlog::error("Node {} is not a sequence!", _key);
          std::exit(-1);
        }
        node.reset(node[_key]);

        for (auto& i : indices) {
          if (i > node.size()) {
            spdlog::error("Sequence access out of bound! To append elements to a sequence, use the index as one past the end of the sequence.");
            std::exit(-1);
          }
          node.reset(node[i]);
        }
      }
    }

    // Set the value to the key and start over again
    node = value;
    node.reset(config);
  }
}




}   // namespace Ramulator
