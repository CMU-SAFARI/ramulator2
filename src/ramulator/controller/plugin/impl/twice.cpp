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

class TWiCeIdeal : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, TWiCeIdeal, "TWiCeIdeal")

 private:
  ControllerBase* m_ctrl = nullptr;

  struct TwiCeEntry {
    int act_count = -1;
    int life = -1;
    TwiCeEntry() = default;
    TwiCeEntry(int a, int l) : act_count(a), life(l) {}
  };

  int m_twice_rh_threshold = -1;
  float m_twice_pruning_interval_threshold = -1;
  bool m_is_debug = false;

  int m_vrr_cmd_id = -1;
  int m_rank_level = -1;
  int m_bank_level = -1;
  int m_row_level = -1;

  std::vector<std::unordered_map<int, TwiCeEntry>> m_twice_table;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_twice_rh_threshold, int, "twice_rh_threshold").required();
    RAMULATOR_PARSE_PARAM(m_twice_pruning_interval_threshold, float, "twice_pruning_interval_threshold").required();
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "TWiCe is not compatible with the DRAM standard that does not "
          "have Victim-Row-Refresh (VRR) command!");
    }

    m_vrr_cmd_id = spec->get_command_id("VRR");
    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");

    int num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_twice_table.resize(num_banks);
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;

    // On all-bank refresh: prune entries with low activation counts.
    if (spec->command_meta[req.command].is_refreshing &&
        spec->bank_targets[req.command] == BankTarget::All) {
      if (m_is_debug) {
        std::cout << "TWiCeIdeal: Refresh command" << std::endl;
      }
      for (size_t i = 0; i < m_twice_table.size(); i++) {
        std::vector<int> to_prune;
        for (auto& [row_id, entry] : m_twice_table[i]) {
          if (entry.act_count < entry.life * m_twice_pruning_interval_threshold) {
            to_prune.push_back(row_id);
            if (m_is_debug) {
              std::cout << "TWiCeIdeal: Pruned entry " << row_id << " from bank " << i << std::endl;
            }
          } else {
            entry.life++;
            if (m_is_debug) {
              std::cout << "TWiCeIdeal: Incremented life of entry " << row_id << " in bank " << i << std::endl;
            }
          }
        }
        for (int row_id : to_prune) {
          m_twice_table[i].erase(row_id);
        }
      }
      return;
    }

    // On ACT: track activation and check threshold.
    if (spec->command_meta[req.command].is_opening &&
        spec->bank_targets[req.command] == BankTarget::Single) {
      int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
      int row_id = req.addr_vec[m_row_level];

      if (m_is_debug) {
        std::cout << "TWiCeIdeal: ACT on row " << row_id << std::endl;
        std::cout << "  └  " << "rank: " << req.addr_vec[m_rank_level] << std::endl;
        std::cout << "  └  " << "bank_group: " << req.addr_vec[m_rank_level + 1] << std::endl;
        std::cout << "  └  " << "bank: " << req.addr_vec[m_bank_level] << std::endl;
        std::cout << "  └  " << "index: " << bank_id << std::endl;
      }

      auto it = m_twice_table[bank_id].find(row_id);
      if (it == m_twice_table[bank_id].end()) {
        m_twice_table[bank_id].emplace(row_id, TwiCeEntry(1, 0));
        if (m_is_debug) {
          std::cout << "TWiCeIdeal: Inserted row " << row_id << " into bank " << bank_id << std::endl;
        }
      } else {
        it->second.act_count++;
        if (it->second.act_count >= m_twice_rh_threshold) {
          // Issue one VRR with the aggressor's addr_vec
          Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
          m_ctrl->priority_send(vrr_req);
          if (m_is_debug) {
            std::cout << "TWiCeIdeal: VRR on row " << row_id << std::endl;
            std::cout << "  └  " << "rank: " << req.addr_vec[m_rank_level] << std::endl;
            std::cout << "  └  " << "bank_group: " << req.addr_vec[m_rank_level + 1] << std::endl;
            std::cout << "  └  " << "bank: " << req.addr_vec[m_bank_level] << std::endl;
            std::cout << "  └  " << "index: " << bank_id << std::endl;
            std::cout << "TWiCeIdeal: Erased entry " << row_id << " from bank " << bank_id << std::endl;
          }
          m_twice_table[bank_id].erase(it);
        }
      }
    }
  }
};

}  // namespace Ramulator
