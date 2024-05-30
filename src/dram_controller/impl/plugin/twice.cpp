#include <vector>
#include <unordered_map>
#include <limits>
#include <random>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class TWiCeIdeal : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, TWiCeIdeal, "TWiCe-Ideal", "Idealized TWiCe.")

  private:
    IDRAM* m_dram = nullptr;

    struct TwiCeEntry {
      int act_count;
      int life;
      TwiCeEntry():
        act_count(-1), life(-1) {};
      TwiCeEntry(int a, int l):
        act_count(a), life(l) {};
    };

    Clk_t m_clk = 0;

    int m_twice_rh_threshold = -1;
    float m_twice_pruning_interval_threshold = -1;
    bool m_is_debug = false;

    int m_VRR_req_id = -1;

    int m_rank_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;
    
    // per bank twice table
    // indexed using flattened <rank id, bank id>
    // e.g., if rank 0, bank 4, index is 4
    // if rank 1, bank 5, index is 16 (assuming 16 banks/rank) + 5
    std::vector<std::unordered_map<Addr_t, TwiCeEntry>> m_twice_table;

  public:
    void init() override { 
      m_twice_rh_threshold = param<int>("twice_rh_threshold").required();
      m_twice_pruning_interval_threshold = param<float>("twice_pruning_interval_threshold").required();
      m_is_debug = param<bool>("debug").default_val(false);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;

      if (!m_dram->m_commands.contains("VRR")) {
        throw ConfigurationError("TWiCe is not compatible with the DRAM implementation that does not have Victim-Row-Refresh (VRR) command!");
      }

      m_VRR_req_id = m_dram->m_requests("victim-row-refresh");

      m_rank_level = m_dram->m_levels("rank");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_banks_per_rank = m_dram->get_level_size("bankgroup") == -1 ? 
                             m_dram->get_level_size("bank") : 
                             m_dram->get_level_size("bankgroup") * m_dram->get_level_size("bank");
      m_num_rows_per_bank = m_dram->get_level_size("row");

      // Initialize twice table
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        std::unordered_map<Addr_t, TwiCeEntry> bank_twice_table;
        m_twice_table.push_back(bank_twice_table);
      }
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override {

      m_clk++;

      if (request_found) {
        if (m_dram->m_command_meta(req_it->command).is_refreshing && m_dram->m_command_scopes(req_it->command) == m_rank_level) {
          // Refresh command
          // TODO: we can get pruning interval as a parameter
          if (m_is_debug) {
            std::cout << "TWiCeIdeal: Refresh command" << std::endl;
          }
          for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
            std::vector<std::unordered_map<Addr_t, TwiCeEntry>::iterator> to_be_pruned;
            for (auto it = m_twice_table[i].begin(); it != m_twice_table[i].end(); it++) {
              if (it->second.act_count < it->second.life * m_twice_pruning_interval_threshold) {
                // Store the entries to be pruned
                to_be_pruned.emplace_back(it);
                if (m_is_debug) {
                  std::cout << "TWiCeIdeal: Pruned entry " << it->first << " from bank " << i << std::endl;
                }
              } else {
                // Increment the life of the entry
                it->second.life++;
                if (m_is_debug) {
                  std::cout << "TWiCeIdeal: Incremented life of entry " << it->first << " in bank " << i << std::endl;
                }
              }
            }
            for (auto&& it : to_be_pruned) {
              m_twice_table[i].erase(it);
            }
          }
        } else if (m_dram->m_command_meta(req_it->command).is_opening && m_dram->m_command_scopes(req_it->command) == m_row_level) {
          // Activation command
          int flat_bank_id = req_it->addr_vec[m_bank_level];
          int accumulated_dimension = 1;
          for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
            accumulated_dimension *= m_dram->m_organization.count[i + 1];
            flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
          }
          
          int row_id = req_it->addr_vec[m_row_level];

          if (m_is_debug) {
            std::cout << "TWiCeIdeal: ACT on row " << row_id << std::endl;
            std::cout << "  └  " << "rank: " << req_it->addr_vec[m_rank_level] << std::endl;
            std::cout << "  └  " << "bank_group: " << req_it->addr_vec[m_rank_level + 1] << std::endl;
            std::cout << "  └  " << "bank: " << req_it->addr_vec[m_bank_level] << std::endl;
            std::cout << "  └  " << "index: " << flat_bank_id << std::endl;
          }

          if (m_twice_table[flat_bank_id].find(row_id) == m_twice_table[flat_bank_id].end()){
            // If row is not in the table, insert it
            m_twice_table[flat_bank_id].insert(std::make_pair(row_id, TwiCeEntry(1, 0)));
            
            if (m_is_debug) {
              std::cout << "TWiCeIdeal: Inserted row " << row_id << " into bank " << flat_bank_id << std::endl;
            }
          } else {
            // If row is in the table, increment the act count
            m_twice_table[flat_bank_id][row_id].act_count++;

            if (m_twice_table[flat_bank_id][row_id].act_count >= m_twice_rh_threshold) {
              // If the act count is greater than the threshold, issue a VRR
              Request vrr_req(req_it->addr_vec, m_VRR_req_id);
              m_ctrl->priority_send(vrr_req);

              auto it = m_twice_table[flat_bank_id].find(row_id);
              m_twice_table[flat_bank_id].erase(it);

              if (m_is_debug) {
                std::cout << "TWiCeIdeal: VRR on row " << row_id << std::endl;
                std::cout << "  └  " << "rank: " << req_it->addr_vec[m_rank_level] << std::endl;
                std::cout << "  └  " << "bank_group: " << req_it->addr_vec[m_rank_level + 1] << std::endl;
                std::cout << "  └  " << "bank: " << req_it->addr_vec[m_bank_level] << std::endl;
                std::cout << "  └  " << "index: " << flat_bank_id << std::endl;
                std::cout << "TWiCeIdeal: Erased entry " << row_id << " from bank " << flat_bank_id << std::endl;
              }
            }
          }
        }

      }
    };

};

}       // namespace Ramulator
