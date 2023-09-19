#include <vector>
#include <unordered_map>
#include <limits>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class CounterBasedTRR : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, CounterBasedTRR, "CounterBasedTRR", "CounterBasedTRR.")
  private:
    Clk_t m_clk = 0;
    IDeviceSpec* m_spec;
    IDRAMController* m_ctrl;

    int m_dram_org_levels = -1;
    int m_rank_level_idx = -1;
    int m_bankgroup_level_idx = -1;
    int m_bank_level_idx = -1;
    int m_row_level_idx = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_bankgroup = -1;
    int m_num_banks = -1;

    int m_ACT_id = -1;
    int m_RFM_id = -1;
    int m_REF_id = -1;

    int m_size;

    struct BankTable {
      int m_size;
      std::unordered_map<int, int> m_counters;

      int m_max_act_row_addr = -1;
      int m_max_act_count = 0;
      int m_min_act_row_addr = -1;
      int m_min_act_count = 0;

      BankTable(int size): m_size(size) {};

      void processACT(int row_addr) {
        if (auto it = m_counters.find(row_addr); it != m_counters.end()) {
          // Row is already in the table
          auto& [addr, count] = *it;
          count++;

          if (row_addr == m_min_act_row_addr) {
            // See if we are still the smallest
            for (auto& [_addr, _count] : m_counters) {
              if (_count < count) {
                m_min_act_count = _count;
                m_min_act_row_addr = _addr;
                break;
              }
            }
          }

          if (count > m_max_act_count) {
            m_max_act_count = count;
            m_max_act_row_addr = row_addr;
          }
        } else {
          // Row not in the table
          if (m_counters.size() < m_size) {
            // Still have space in the table
            m_counters[row_addr] = 1;
            m_min_act_count = 1;
            m_min_act_row_addr = row_addr;
          } else {
            // Need to evict the smallest entry
            auto min_entry = m_counters.extract(m_min_act_row_addr);
            min_entry.key() = row_addr;
            m_counters.insert(std::move(min_entry));
          }
        }
      }

      void processRFM() {
        m_counters.erase(m_max_act_row_addr);
        m_max_act_count = 0;
        for (auto& [addr, count] : m_counters) {
          if (count > m_max_act_count) {
            m_max_act_row_addr = addr;
            m_max_act_count = count;
          }
        }
      };
    };

    std::vector<std::vector<BankTable>> m_bank_tables;
    std::vector<std::vector<int>> m_bank_counters;

  public:
    void init() override { 
      m_ctrl = cast_parent<IDRAMController>();
      m_size = param<int>("table_size").desc("Number of entries per bank-level TRR table").default_val(16);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_spec = m_ctrl->m_device->m_spec;

      m_dram_org_levels = m_spec->get_level_defs().size();
      m_rank_level_idx = m_spec->get_level_defs().get_id_of("rank");
      m_row_level_idx = m_spec->get_level_defs().get_id_of("row");

      try {
        m_bankgroup_level_idx = m_spec->get_level_defs().get_id_of("bankgroup");
        m_num_banks_per_bankgroup = m_spec->organization.count[m_bank_level_idx];
      } catch (const std::runtime_error& e) { }

      m_bank_level_idx = m_spec->get_level_defs().get_id_of("bank");
      m_num_ranks = m_spec->organization.count[m_rank_level_idx];
      m_num_banks = (m_bankgroup_level_idx == -1) ? 
                    m_spec->organization.count[m_bank_level_idx] :
                    m_spec->organization.count[m_bankgroup_level_idx] * m_spec->organization.count[m_bank_level_idx];

      m_bank_tables.resize(m_num_ranks, std::vector<BankTable>(m_num_banks, BankTable(m_size)));
      m_bank_counters.resize(m_num_ranks, std::vector<int>(m_num_banks, 0));

      m_ACT_id = m_spec->get_command_defs().get_id_of("ACT");
      // m_RFM_id = m_spec->get_command_defs().get_id_of("RFM");
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) {
      m_clk++;

      if (request_found) {
        int rank_id = req_it->addr_vec[m_rank_level_idx];
        if (req_it->command == m_ACT_id) {
          int bank_id = -1;
          if (m_bankgroup_level_idx == -1) {
            bank_id = req_it->addr_vec[m_bank_level_idx];
          } else {
            int bank_groupid = req_it->addr_vec[m_bankgroup_level_idx];
            bank_id = bank_groupid * m_num_banks_per_bankgroup + req_it->addr_vec[m_bank_level_idx];
          }
          int row_addr = req_it->addr_vec[m_row_level_idx];
          m_bank_tables[rank_id][bank_id].processACT(row_addr);
        } 
      }

      // else if (req_it->command == m_RFM_id) {
      //   for (auto& bank_table : m_bank_tables[rank_id]) {
      //     bank_table.processRFM();
      //   }
      // }
    };

};

}       // namespace Ramulator
