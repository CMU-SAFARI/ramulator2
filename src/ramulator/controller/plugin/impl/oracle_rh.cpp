#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class OracleRH : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, OracleRH, "OracleRH")

 private:
  ControllerBase* m_ctrl = nullptr;

  int m_rh_threshold = -1;
  bool m_is_debug = false;

  int m_vrr_cmd_id = -1;
  int m_rank_level = -1;
  int m_row_level = -1;
  int m_num_banks_per_rank = -1;

  using BankACTCounter = std::unordered_map<int, int>;
  std::vector<BankACTCounter> m_table;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_rh_threshold, int, "tRH").required();
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "OracleRH is not compatible with the DRAM standard that does not "
          "have Victim-Row-Refresh (VRR) command!");
    }

    m_vrr_cmd_id = spec->get_command_id("VRR");
    m_rank_level = spec->get_level_id("Rank");
    m_row_level = spec->get_level_id("Row");

    int num_banks = m_ctrl->m_device.m_bank_nodes.size();
    int num_ranks = spec->get_level_size("Rank");
    m_num_banks_per_rank = num_banks / num_ranks;
    m_table.resize(num_banks);
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;

    // On ACT: increment row counter, issue VRR if threshold reached.
    if (spec->command_meta[req.command].is_opening &&
        spec->bank_targets[req.command] == BankTarget::Single) {
      int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
      int row_id = req.addr_vec[m_row_level];
      auto& count = m_table[bank_id][row_id];
      count++;
      if (count >= m_rh_threshold) {
        count = 0;
        // issue VRR with the aggressor's addr_vec;
        Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
        m_ctrl->priority_send(vrr_req);
        if (m_is_debug) {
          std::cout << "OracleRH: VRR fired on bank=" << bank_id
                    << " row=" << row_id << std::endl;
        }
      }
    }

    // On all-bank refresh: clear counters for that rank
    if (spec->command_meta[req.command].is_refreshing &&
        spec->bank_targets[req.command] == BankTarget::All) {
      int rank_id = req.addr_vec[m_rank_level];
      for (int i = rank_id * m_num_banks_per_rank;
           i < (rank_id + 1) * m_num_banks_per_rank; i++) {
        m_table[i].clear();
      }
    }
  }
};

}  // namespace Ramulator
