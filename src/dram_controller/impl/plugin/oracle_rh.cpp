#include <vector>
#include <deque>
#include <unordered_map>
#include <limits>
#include <random>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class OracleRH : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, OracleRH, "OracleRH", "Oracle RowHammer defense")

  private:
    IDRAM* m_dram = nullptr;

    using BankACTCounter = std::unordered_map<Addr_t, int>;
    std::vector<BankACTCounter> m_table;

    int m_RH_threshold = -1;

    int m_VRR_req_id = -1;

    int m_rank_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;

    bool m_is_debug = false;

  public:
    void init() override { 
      m_is_debug = param<bool>("debug").default_val(false);
      m_RH_threshold = param<int>("tRH").required();
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;

      if (!m_dram->m_commands.contains("VRR")) {
        throw ConfigurationError("OracleRH is not compatible with the DRAM implementation that does not have Victim-Row-Refresh (VRR) command!");
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

      m_table.resize(m_num_banks_per_rank * m_num_ranks);
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      if (request_found) {
        if (
          m_dram->m_command_meta(req_it->command).is_opening && 
          m_dram->m_command_scopes(req_it->command) == m_row_level
        ) {
          int flat_bank_id = req_it->addr_vec[m_bank_level];
          int accumulated_dimension = 1;
          for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
            accumulated_dimension *= m_dram->m_organization.count[i + 1];
            flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
          }
          
          int row_id = req_it->addr_vec[m_row_level];
          if (m_table[flat_bank_id].find(row_id) != m_table[flat_bank_id].end()) {
            m_table[flat_bank_id][row_id]++;
            if (m_table[flat_bank_id][row_id] >= m_RH_threshold) {
              m_table[flat_bank_id][row_id] = 0;
              Request vrr_req(req_it->addr_vec, m_VRR_req_id);
              m_ctrl->priority_send(vrr_req);
            }
          } else {
            m_table[flat_bank_id][row_id] = 1;
          }
        } else if (
          m_dram->m_command_meta(req_it->command).is_refreshing && 
          m_dram->m_command_scopes(req_it->command) == m_rank_level) {
            int rank_id = req_it->addr_vec[m_rank_level];
            for (int i = rank_id * m_num_banks_per_rank; i < (rank_id + 1) * m_num_banks_per_rank; i++) {
              m_table[i].clear();
            }
        }
      }
    };

};

}       // namespace Ramulator
