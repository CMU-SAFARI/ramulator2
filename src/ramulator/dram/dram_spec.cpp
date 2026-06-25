#include "ramulator/dram/dram_spec.h"

#include <stdexcept>

#include <fmt/format.h>

namespace Ramulator {

void DRAMSpec::load_config(const ConfigNode& config) {
  const ConfigNode dram = config["dram"];

  // Organization
  channel_width = dram["channel_width"].as<int>();
  ConfigNode org = dram["org"];
  organization.dq = org["dq"].as<int>();
  ConfigNode count_node = org["count"];
  const auto& counts = count_node.seq();
  organization.level_sizes.resize(counts.size());
  for (size_t i = 0; i < counts.size(); i++) {
    organization.level_sizes[i] = counts[i].as<int>();
  }
  // Each controller handles a single channel
  if (organization.level_sizes[0] != 1) {
    throw std::runtime_error(
        "DRAMSpec: level_sizes[0] (Channel) must be 1 — "
        "multi-channel is configured at the system level, not in the DRAM spec. "
        "Got: " + std::to_string(organization.level_sizes[0]));
  }

  // Timing values
  ConfigNode timing_node = dram["timing"];
  const auto& timing = timing_node.seq();
  timing_vals.resize(timing.size());
  for (size_t i = 0; i < timing.size(); i++) {
    timing_vals[i] = timing[i].as<int>();
  }

  // Read latency (pre-computed by Python)
  read_latency = dram["read_latency"].as<int>();

  // Timing constraints (pre-computed by Python)
  timing_cons.resize(level_count, std::vector<std::vector<TimingConsEntry>>(command_count));

  ConfigNode tc_node = dram["timing_constraints"];
  for (const auto& entry : tc_node.seq()) {
    const auto& f = entry.seq();
    int level = f[0].as<int>();
    int latency = f[3].as<int>();
    int window = f.size() > 4 ? f[4].as<int>() : 1;
    bool sibling = f.size() > 5 ? f[5].as<bool>() : false;

    // Validate the constraint shape before indexing timing_cons. A
    // mistyped or out-of-range level or command id would otherwise
    // silently corrupt adjacent vectors via timing_cons[level][cmd_id]
    // (no bounds check). A window of 0 is even worse: the per-node
    // m_cmd_history is sized to `max(window) = 0` and then
    // update_timing reads m_cmd_history[command][window - 1] = [-1] —
    // out-of-bounds on an empty deque, undefined behavior in the
    // simulation hot path.
    if (level < 0 || level >= level_count) {
      throw std::runtime_error(fmt::format(
          "DRAMSpec: timing_constraints level {} is outside [0, {})",
          level, level_count));
    }
    if (window <= 0) {
      throw std::runtime_error(fmt::format(
          "DRAMSpec: timing_constraints window must be >= 1 (got {}); a "
          "window of 0 would size the history to 0 and turn the "
          "node-level update into out-of-bounds deque access",
          window));
    }
    auto require_cmd_id = [&](int cmd_id, const char* role) {
      if (cmd_id < 0 || cmd_id >= command_count) {
        throw std::runtime_error(fmt::format(
            "DRAMSpec: timing_constraints {} command id {} is outside [0, {})",
            role, cmd_id, command_count));
      }
    };
    for (const auto& p : f[1].seq()) {
      int preceding_cmd = p.as<int>();
      require_cmd_id(preceding_cmd, "preceding");
      for (const auto& fc : f[2].seq()) {
        int following_cmd = fc.as<int>();
        require_cmd_id(following_cmd, "following");
        timing_cons[level][preceding_cmd].push_back({following_cmd, latency, window, sibling});
      }
    }
  }

  // Precompute sibling-constraint flags to skip unnecessary traversal
  has_sibling_cons.assign(level_count, std::vector<int8_t>(command_count, 0));
  for (int lvl = 0; lvl < level_count; ++lvl) {
    for (int cmd = 0; cmd < command_count; ++cmd) {
      for (const auto& t : timing_cons[lvl][cmd]) {
        if (t.sibling) {
          has_sibling_cons[lvl][cmd] = 1;
          break;
        }
      }
    }
  }
}

std::map<std::string, DRAMSpec::Creator>& DRAMSpec::registry() {
  static std::map<std::string, Creator> r;
  return r;
}

bool DRAMSpec::register_standard(const std::string& name, Creator c) {
  registry()[name] = std::move(c);
  return true;
}

std::unique_ptr<DRAMSpec> DRAMSpec::create(const std::string& name, const ConfigNode& config) {
  auto it = registry().find(name);
  if (it == registry().end()) {
    throw std::runtime_error("Unknown DRAM standard: " + name);
  }
  auto spec = it->second(config);
  spec->standard_name = name;
  return spec;
}

}  // namespace Ramulator
