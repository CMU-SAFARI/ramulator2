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

class Graphene : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, Graphene, "Graphene")

 private:
  ControllerBase* m_ctrl = nullptr;

  int m_clk = 0;

  int m_num_table_entries = -1;
  int m_activation_threshold = -1;
  int m_reset_period_ns = -1;
  int m_reset_period_clk = -1;
  bool m_is_debug = false;

  int m_vrr_cmd_id = -1;
  int m_rank_level = -1;
  int m_bank_level = -1;
  int m_row_level = -1;
  int m_num_rows_per_bank = -1;

  // Per-bank activation count table
  std::vector<std::unordered_map<int, int>> m_activation_count_table;
  // Per-bank spillover counter
  std::vector<int> m_spillover_counter;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_num_table_entries, int, "num_table_entries").required();
    RAMULATOR_PARSE_PARAM(m_activation_threshold, int, "activation_threshold").required();
    RAMULATOR_PARSE_PARAM(m_reset_period_ns, int, "reset_period_ns").required();
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "Graphene is not compatible with the DRAM standard that does not "
          "have Victim-Row-Refresh (VRR) command!");
    }

    m_reset_period_clk = m_reset_period_ns / ((float)spec->get_timing_value("tCK_ps") / 1000.0f);

    m_vrr_cmd_id = spec->get_command_id("VRR");
    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");

    int num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_num_rows_per_bank = spec->get_level_size("Row");

    // Initialize per-bank tables with dummy entries (negative row IDs)
    for (int i = 0; i < num_banks; i++) {
      std::unordered_map<int, int> table;
      for (int j = -m_num_rows_per_bank; j < -m_num_rows_per_bank + m_num_table_entries; j++) {
        table[j] = 0;
      }
      m_activation_count_table.push_back(table);
    }

    m_spillover_counter.assign(num_banks, 0);
  }

  void pre_schedule() override {
    m_clk++;

    // Periodic reset of all tables and spillover counters
    if (m_clk % m_reset_period_clk == 0) {
      for (size_t i = 0; i < m_activation_count_table.size(); i++) {
        for (auto& [row_id, count] : m_activation_count_table[i]) {
          count = 0;
        }
        m_spillover_counter[i] = 0;
      }
    }
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->command_meta[req.command].is_opening) return;
    if (spec->bank_targets[req.command] != BankTarget::Single) return;

    int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    int row_id = req.addr_vec[m_row_level];

    if (m_is_debug) {
      std::cout << "Graphene: ACT on row " << row_id << std::endl;
      std::cout << "  └  " << "rank: " << req.addr_vec[m_rank_level] << std::endl;
      std::cout << "  └  " << "bank_group: " << req.addr_vec[m_rank_level + 1] << std::endl;
      std::cout << "  └  " << "bank: " << req.addr_vec[m_bank_level] << std::endl;
      std::cout << "  └  " << "index: " << bank_id << std::endl;
    }

    auto it = m_activation_count_table[bank_id].find(row_id);
    if (it == m_activation_count_table[bank_id].end()) {
      // Row not in table — find an entry with count, spillover counter to evict
      bool found = false;
      int to_remove = -1;
      int spillover_value = -1;

      for (auto& [rid, count] : m_activation_count_table[bank_id]) {
        if (m_is_debug) {
          std::cout << "  └  " << "checking row " << rid << " with count " << count << std::endl;
        }
        if (count == m_spillover_counter[bank_id]) {
          spillover_value = count;
          to_remove = rid;
          found = true;
          break;
        }
      }

      if (found) {
        if (m_is_debug) {
          std::cout << "Removing row " << to_remove << " from table " << bank_id << std::endl;
          std::cout << "Adding row " << row_id << " to table " << bank_id << std::endl;
          std::cout << "  └  " << "spillover counter: " << m_spillover_counter[bank_id] << std::endl;
        }
        m_activation_count_table[bank_id].erase(to_remove);
        m_activation_count_table[bank_id][row_id] = spillover_value + 1;
      } else {
        // No evictable entry — increment spillover counter
        m_spillover_counter[bank_id]++;
      }
    } else {
      // Row in table — increment count
      it->second++;

      if (m_is_debug) {
        std::cout << "Row " << row_id << " in table[" << bank_id << "]" << std::endl;
        std::cout << "  └  " << "threshold: " << m_activation_threshold << std::endl;
        std::cout << "  └  " << "count: " << it->second << std::endl;
      }

      if (it->second >= m_activation_threshold) {
        if (m_is_debug) {
          std::cout << "Row " << row_id << " in table " << bank_id
                    << " has exceeded the threshold!" << std::endl;
        }
        // Issue one VRR with the aggressor's addr_vec
        Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
        m_ctrl->priority_send(vrr_req);
        it->second = m_spillover_counter[bank_id];
      }
    }
  }
};

}  // namespace Ramulator
