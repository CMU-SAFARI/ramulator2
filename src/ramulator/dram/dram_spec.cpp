#include "ramulator/dram/dram_spec.h"

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
    for (const auto& p : f[1].seq()) {
      for (const auto& fc : f[2].seq()) {
        timing_cons[level][p.as<int>()].push_back({fc.as<int>(), latency, window, sibling});
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
  return it->second(config);
}

}  // namespace Ramulator
