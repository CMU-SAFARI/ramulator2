#ifndef RAMULATOR_DRAM_DRAM_SPEC_H
#define RAMULATOR_DRAM_DRAM_SPEC_H

#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/config_node.h"
#include "ramulator/base/type.h"
#include "ramulator/dram/func_types.h"

namespace Ramulator {

// Bank-targeting pattern for command state dispatch
enum class BankTarget {
  Single,    // One specific bank (ACT, PREpb, RD, WR, RDA, WRA)
  All,       // All banks under scope (PREab, REFab, RFMab)
  SameBank,  // Same bank ID in every bank group (PREsb, REFsb — DDR5)
};

// Organization hierarchy of the device
struct Organization {
  /// The DQ width.
  int dq = -1;
  /// The size of different levels in the hierarchy.
  std::vector<int> level_sizes;
};

// Meta information about a command
struct DRAMCommandMeta {
  bool is_opening = false;
  bool is_closing = false;
  bool is_accessing = false;
  bool is_refreshing = false;
  bool is_row_command = false;     // Row bus (HBM/HBM2: ACT, PREpb, PREab, REFab, REFpb)
  bool is_column_command = false;  // Column bus (HBM/HBM2: RD, WR, RDA, WRA)
};

// Timing Constraint
struct TimingConsEntry {
  /// The command that the timing constraint is constraining.
  int cmd;
  /// The value of the timing constraint (in number of cycles).
  int val;
  /// How long of a history to keep track of?
  int window = 1;
  /// Whether this timing constraint is affecting siblings in the same level.
  bool sibling = false;

  TimingConsEntry(int cmd, int val, int window = 1, bool sibling = false)
      : cmd(cmd), val(val), window(window), sibling(sibling) {
    if (this->window < 0) {
      throw std::runtime_error("TimingConsEntry: window value < 0");
    }
  };
};

using TimingCons = std::vector<std::vector<std::vector<TimingConsEntry>>>;

// Runtime representation of a DRAM standard's specification.
// Base class for concrete standards (e.g., DDR4) which self-populate
// in their constructor from static enums + config.
struct DRAMSpec {
  virtual ~DRAMSpec() = default;

  // String -> int maps for named lookups.
  // Components query these in init() and cache the result.
  std::unordered_map<std::string, int> levels;    // "Row" -> 4
  std::unordered_map<std::string, int> commands;  // "ACT" -> 0
  std::unordered_map<std::string, int> states;    // "Opened" -> 0
  std::unordered_map<std::string, int> timings;   // "tCK_ps" -> 20

  // Reverse lookup vectors (int -> string), populated alongside the maps above.
  std::vector<std::string> level_names;    // 0 -> "Channel", 1 -> "Rank", ...
  std::vector<std::string> command_names;  // 0 -> "ACT", 1 -> "PREpb", ...
  std::vector<std::string> state_names;    // 0 -> "Opened", 1 -> "Closed", ...
  std::vector<std::string> timing_names;   // 0 -> "rate", 1 -> "nBL", ...

  // Counts
  int level_count = -1;
  int command_count = -1;
  int state_count = -1;
  int timing_count = -1;

  // Constants
  std::string standard_name;
  int internal_prefetch_size = -1;
  int channel_width = -1;
  int data_payload_bytes = -1;
  Clk_t read_latency = -1;

  // Per-level/command arrays
  Organization organization;
  std::vector<int> init_states;         // per level
  std::vector<int> supported_requests;  // per request type
  std::vector<int> timing_vals;         // per timing parameter
  std::vector<int> command_cycles;      // per command: CA bus cycles (ticks)
  TimingCons timing_cons;                // per level x command
  std::vector<std::vector<int8_t>> has_sibling_cons;  // [level][cmd] → has sibling timing constraint
  std::vector<DRAMCommandMeta> command_meta;
  std::vector<BankTarget> bank_targets;

  // Non-templated function arrays (action, preq, rowhit, rowopen)
  FuncArrays funcs;

  // Existence checks — use at init-time for optional features.
  bool has_level(const std::string& name) const { return levels.count(name); }
  bool has_command(const std::string& name) const { return commands.count(name); }
  bool has_state(const std::string& name) const { return states.count(name); }
  bool has_timing(const std::string& name) const { return timings.count(name); }

  // Checked lookups — throw std::runtime_error if not found.
  // Use at init-time and cache the result. Never call on a hot path.
  int get_level_id(const std::string& name) const {
    auto it = levels.find(name);
    if (it == levels.end()) throw std::runtime_error("DRAMSpec: unknown level '" + name + "'");
    return it->second;
  }
  int get_command_id(const std::string& name) const {
    auto it = commands.find(name);
    if (it == commands.end()) throw std::runtime_error("DRAMSpec: unknown command '" + name + "'");
    return it->second;
  }
  int get_state_id(const std::string& name) const {
    auto it = states.find(name);
    if (it == states.end()) throw std::runtime_error("DRAMSpec: unknown state '" + name + "'");
    return it->second;
  }

  // Combined timing lookup + value access — throws if not found.
  int get_timing_value(const std::string& name) const {
    auto it = timings.find(name);
    if (it == timings.end()) throw std::runtime_error("DRAMSpec: unknown timing '" + name + "'");
    return timing_vals[it->second];
  }

  // Combined level lookup + organization count — throws if not found.
  int get_level_size(const std::string& name) const {
    return organization.level_sizes[get_level_id(name)];
  }

  // Bytes per DRAM transaction. Some standards transfer non-payload metadata
  // bits on the wire, so a payload override can differ from the physical width.
  int get_tx_bytes() const {
    return data_payload_bytes > 0 ? data_payload_bytes : internal_prefetch_size * channel_width / 8;
  }


  // Helper for populating name maps + reverse vectors in subclass constructors.
  void set_names(std::unordered_map<std::string, int>& map, std::vector<std::string>& name_vec,
                 std::initializer_list<const char*> names) {
    int i = 0;
    name_vec.clear();
    name_vec.reserve(names.size());
    for (auto* n : names) {
      map[n] = i++;
      name_vec.push_back(n);
    }
  }

  // Load runtime config data (organization, timing, etc.).
  // Defined in dram_spec.cpp.
  void load_config(const ConfigNode& config);

  // Factory registry — maps DRAM standard name (e.g., "DDR4") to creator.
  using Creator = std::function<std::unique_ptr<DRAMSpec>(const ConfigNode&)>;

  static std::map<std::string, Creator>& registry();
  static bool register_standard(const std::string& name, Creator c);
  static std::unique_ptr<DRAMSpec> create(const std::string& name, const ConfigNode& config);
};

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_DRAM_SPEC_H
