#include <fmt/format.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/// Counts specified DRAM commands and writes totals to a CSV file at finalization.
///
/// Example config (Python):
///   ramulator.ControllerPlugin.CommandCounter(
///       commands_to_count=["ACT", "RD", "WR", "PREpb"],
///       path="cmd_counts.csv",
///   )
///
/// Output format (one line per command):
///   ACT, 12345
///   RD, 6789
class CommandCounter : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, CommandCounter, "CommandCounter")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_commands_to_count, std::vector<std::string>, "commands_to_count").required();
    RAMULATOR_PARSE_PARAM(m_path, std::string, "path").required();
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;
    for (const auto& name : m_commands_to_count) {
      int cmd_id = spec.get_command_id(name);
      m_counters[cmd_id] = 0;
    }
  }

  void on_issue(const Request& req) override {
    auto it = m_counters.find(req.command);
    if (it != m_counters.end()) {
      it->second++;
    }
  }

  void finalize() override {
    const auto& cmd_names = m_ctrl->m_device.m_spec->command_names;
    std::ofstream out(m_path);
    for (const auto& [cmd_id, count] : m_counters) {
      out << fmt::format("{}, {}\n", cmd_names[cmd_id], count);
    }
  }

 private:
  ControllerBase* m_ctrl = nullptr;
  std::vector<std::string> m_commands_to_count;
  std::string m_path;
  std::unordered_map<int, size_t> m_counters;
};

}  // namespace Ramulator
