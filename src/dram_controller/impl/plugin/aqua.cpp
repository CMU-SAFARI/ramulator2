#include <vector>
#include <unordered_map>
#include <limits>
#include <random>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "translation/translation.h"
#include "addr_mapper/impl/rit.h"
#include "dram_controller/impl/plugin/device_config/device_config.h"

namespace Ramulator {

class AQUA : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, AQUA, "AQUA", "AQUA.")

  private:
    IDRAM* m_dram = nullptr;
    LinearMapperBase_with_rit* m_addr_mapper = nullptr;
    ITranslation* m_translation = nullptr;
    DeviceConfig m_cfg;

    Clk_t m_clk = 0;

    int m_num_art_entries = -1;
    int m_num_fpt_entries = -1;
    int m_num_qrows_per_bank = -1;
    int m_art_threshold = -1;
    int m_reset_period_ns = -1;
    Clk_t m_reset_period_clk = -1;
    bool m_is_debug = false;

    int m_rqa_head = 0;

    int m_RD_req_id = -1;
    int m_WR_req_id = -1;

    int m_rank_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;
    int m_col_level = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;
    int m_num_cls = -1;

    // per bank hot-row tracker (same as Graphene)
    // indexed using flattened <rank id, bank id>
    // e.g., if rank 0, bank 4, index is 4
    // if rank 1, bank 5, index is 16 (assuming 16 banks/rank) + 5
    std::vector<std::unordered_map<int, int>> m_aggressor_row_tracker;
    // spillover counter per bank
    std::vector<int> m_spillover_counter;
    // per bank row indirection table is implemented in 'src/addr_mapper/impl/linear_mappers_with_rit.cpp'

    std::vector<std::unordered_map<int, int>> m_reverse_pointer_table;

    // rng
    std::mt19937 generator;
    std::uniform_int_distribution<int> distribution;

    // statistics
    int s_num_migrations = 0;
    int s_num_r_migrations = 0;

  public:
    void init() override { 
      m_num_art_entries = param<int>("num_art_entries").required();
      m_num_fpt_entries = param<int>("num_fpt_entries").required();
      m_num_qrows_per_bank = param<int>("num_qrows_per_bank").required();
      m_art_threshold = param<int>("art_threshold").required();
      m_reset_period_ns = param<int>("reset_period_ns").required();
      m_is_debug = param<bool>("debug").default_val(false);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;
      m_addr_mapper = (LinearMapperBase_with_rit*) memory_system->get_ifce<IAddrMapper>();
      m_translation = frontend->get_ifce<ITranslation>();

      m_cfg.set_device(m_ctrl);

      m_reset_period_clk = m_reset_period_ns / ((float) m_dram->m_timing_vals("tCK_ps") / 1000.0f);

      m_RD_req_id = m_dram->m_requests("read");
      m_WR_req_id = m_dram->m_requests("write");

      m_rank_level = m_dram->m_levels("rank");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");
      m_col_level = m_dram->m_levels("column");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_banks_per_rank = m_dram->get_level_size("bankgroup") == -1 ? 
                             m_dram->get_level_size("bank") : 
                             m_dram->get_level_size("bankgroup") * m_dram->get_level_size("bank");
      m_num_rows_per_bank = m_dram->get_level_size("row");
      m_num_cls = m_dram->get_level_size("column") / 8;

      // Initialize hot-row tracker
      for (int i = 0; i < m_num_banks_per_rank * m_num_ranks; i++) {
        std::unordered_map<int, int> table;
        m_aggressor_row_tracker.push_back(table);

        std::unordered_map<int, int> rpt;
        m_reverse_pointer_table.push_back(rpt);
      }
      // Initialize spillover counter
      m_spillover_counter = std::vector<int>(m_num_banks_per_rank * m_num_ranks, 0);
      // Initialize row indirection table in the addr_mapper
      m_addr_mapper->init_rit(m_num_banks_per_rank * m_num_ranks, m_num_fpt_entries * 2);

      reserve_rows_for_aqua();
      
      // setup random number generator
      generator = std::mt19937(1337);
      distribution = std::uniform_int_distribution<int>(0, m_num_rows_per_bank-1);

      // Register statistics
      register_stat(s_num_migrations).name("aqua_migrations");
      register_stat(s_num_r_migrations).name("aqua_r_migrations");

      if (m_is_debug) {
        std::cout << "AQUA is implemented." << std::endl
                  << "ART size: " << m_num_art_entries << std::endl
                  << "FPT size: " << m_num_fpt_entries << std::endl
                  << "ART threshold: " << m_art_threshold << std::endl
                  << "Number of rows: " << m_num_rows_per_bank << std::endl
                  << "Number of banks: " << m_num_ranks * m_num_banks_per_rank << std::endl
                  << "------------------------------------------------" << std::endl;
      }
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      // Tick myself
      m_clk++;

      if (m_clk % m_reset_period_clk == 0) {
        // Reset hrt and unlock rit
        for (int i = 0; i < m_num_banks_per_rank * m_num_ranks; i++) {
          m_aggressor_row_tracker[i].clear();
          m_spillover_counter[i] = 0;
          // m_addr_mapper->rit_unlock();
        }
      }

      if (request_found) {
        if (m_dram->m_command_meta(req_it->command).is_opening && m_dram->m_command_scopes(req_it->command) == m_row_level) {
          int flat_bank_id = req_it->addr_vec[m_bank_level];
          int accumulated_dimension = 1;
          for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
            accumulated_dimension *= m_dram->m_organization.count[i + 1];
            flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
          }
          
          int row_id = req_it->addr_vec[m_row_level];

          if (m_is_debug) {
            std::cout << "----------------------------" << std::endl;
            std::cout << "AQUA: ACT on row " << row_id << "         " << m_clk << std::endl;
            std::cout << "  └  " << "bank: " << flat_bank_id << std::endl;
          }

          // Check HRT
          if (m_aggressor_row_tracker[flat_bank_id].find(row_id) == m_aggressor_row_tracker[flat_bank_id].end()) {
            if (m_is_debug) {
              std::cout << "  └  " << "row " << row_id << " not in HRT." << std::endl;
            }
            // if row is not in the table, check if the table is full 
            if (m_aggressor_row_tracker[flat_bank_id].size() < m_num_art_entries) {
              if (m_is_debug) {
                std::cout << "  └  " << "HRT is not full, inserting with count 1." << std::endl;
              }
              // if table is not full, insert the row
              m_aggressor_row_tracker[flat_bank_id][row_id] = 1;
            } else {
              if (m_is_debug) {
                std::cout << "  └  " << "HRT is full, searching for a row to evict." << std::endl;
              }
              // if table is full, find a row to evict
              bool found = false;
              int to_remove = -1;
              int spillover_value = -1;

              for (auto it = m_aggressor_row_tracker[flat_bank_id].begin(); it != m_aggressor_row_tracker[flat_bank_id].end(); it++) {
                // if we find an entry with spillover counter value, evict it
                if (it->second == m_spillover_counter[flat_bank_id]) {
                  if (m_is_debug) {
                    std::cout << "  └  " << "found a row to evict: " << it->first << std::endl;
                  }
                  // if we find an entry, record it
                  spillover_value = it->second;
                  to_remove = it->first;
                  found = true;
                  break;
                }
              }

              if (found) {
                if (m_is_debug) {
                  std::cout << "Removing row " << to_remove << " from HRT." << std::endl;
                  std::cout << "Adding row " << row_id << " to HRT." << std::endl;
                }
                // remove to_remove from the table
                m_aggressor_row_tracker[flat_bank_id].erase(to_remove);
                // add row_id to the table
                m_aggressor_row_tracker[flat_bank_id][row_id] = spillover_value + 1;
              }
              else {
                if (m_is_debug) {
                  std::cout << "  └  " << "no row to evict, incrementing spillover counter." << std::endl;
                }
                m_spillover_counter[flat_bank_id] += 1;
                return;
              }
            }
          } else {
            if (m_is_debug) { 
              std::cout << "  └  " << "row " << row_id << " in HRT. Incrementing its counter." << std::endl;
            }
            // if row in table, increment its activation count
            m_aggressor_row_tracker[flat_bank_id][row_id] += 1;
          }
          // dump HRT for debug
          // if (m_is_debug) {
          //   std::cout << "==========================" << std::endl;
          //   std::cout << "HRT[" << flat_bank_id << "].size(): " << m_hot_row_tracker[flat_bank_id].size() << std::endl;
          //   for (auto entry: m_hot_row_tracker[flat_bank_id]) {
          //     std::cout << entry.first << ":\t" << entry.second << std::endl; 
          //   }
          //   std::cout << "Spillover counter: " << m_spillover_counter[flat_bank_id] << std::endl;
          //   std::cout << "==========================" << std::endl;
          // }

          // row is now in the table, check if the count exceeds the threshold
          if (m_is_debug) {
            std::cout << "Row " << row_id << " in ART" << std::endl;
            std::cout << "  └  " << "threshold: " << m_art_threshold << std::endl;
            std::cout << "  └  " << "count: " << m_aggressor_row_tracker[flat_bank_id][row_id] << std::endl;
          }
          if (m_aggressor_row_tracker[flat_bank_id][row_id] % m_art_threshold == 0) {
            if (m_is_debug) {
              std::cout << "Row " << row_id << " needs quarantine!" << std::endl;
              std::cout << "  └  " << "RQA head: " << m_rqa_head << std::endl;
            }
            // issue migration

            // check if the rqa head is already stores another row
            if (m_reverse_pointer_table[flat_bank_id].find(m_rqa_head) != m_reverse_pointer_table[flat_bank_id].end()) {
              // head is valid, it is from the previous epoch (guaranteed by AQUA)
              // evict it than issue the new migration
              if (m_is_debug) {
                std::cout << "RQA head is valid, evicting row " << m_rqa_head << std::endl;
              }
              int prev_q_row = m_rqa_head;
              int prev_org_row = m_reverse_pointer_table[flat_bank_id][m_rqa_head];
              // remove the entry from the RPT
              m_reverse_pointer_table[flat_bank_id].erase(m_rqa_head);
              // remove from FPT
              m_addr_mapper->rit_remove_entry(flat_bank_id, prev_q_row, prev_org_row);

              // issue migration
              issue_migration(req_it, prev_q_row, prev_org_row);
              s_num_r_migrations++;
            } else {
              // quarantine row is empty, issue migration
              if (m_is_debug) {
                std::cout << "RQA head is empty, issuing migration." << std::endl;
              }
            }
            // issue new migration
            issue_migration(req_it, row_id, m_rqa_head);

            // update RPT and FPT
            if (row_id < m_num_qrows_per_bank){ // row is migrated in this epoch
              // find the original row id
              int org_row_id = m_reverse_pointer_table[flat_bank_id][row_id];
              // remove the entry from the RPT and FPT
              m_reverse_pointer_table[flat_bank_id].erase(row_id);
              m_addr_mapper->rit_remove_entry(flat_bank_id, row_id, org_row_id);
              // insert the entry into the RPT and FPT
              m_reverse_pointer_table[flat_bank_id][m_rqa_head] = org_row_id;
              m_addr_mapper->rit_insert_entry(flat_bank_id, m_rqa_head, org_row_id);
            }
            else{ // row is not migrated in this epoch
              // insert the entry into the RPT and FPT
              m_reverse_pointer_table[flat_bank_id][m_rqa_head] = row_id;
              m_addr_mapper->rit_insert_entry(flat_bank_id, row_id, m_rqa_head);
            }
            
            s_num_migrations++;
            // update rqa head
            m_rqa_head = (m_rqa_head + 1) % m_num_qrows_per_bank;
          }
        }
      }
    }

    void issue_migration(ReqBuffer::iterator& req_it, int src_row, int dst_row) {
      // load addr_vec
      std::vector<int> addr_vec;
      for (int i = 0; i < req_it->addr_vec.size(); i++){
        addr_vec.push_back(req_it->addr_vec[i]);
      }

      // Read src_row to copy buffer 
      addr_vec[m_row_level] = src_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_read_req(addr_vec, m_RD_req_id);
        if (!m_ctrl->priority_send(swap_read_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }

      // Write copy buffer to dst_row
      addr_vec[m_row_level] = dst_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_write_req(addr_vec, m_WR_req_id);
        if (!m_ctrl->priority_send(swap_write_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }
    }

    void reserve_rows_for_aqua() {
      Addr_t max_addr = m_translation->get_max_addr();
      // traverse all cls and reserve them if they use rows that store RCT
      Request req(0, 0);
      for (Addr_t addr = 0; addr < max_addr; addr += 64) {
        // apply address mapping
        req.addr = addr;
        m_addr_mapper->apply(req);
        Addr_t row_id = req.addr_vec[m_row_level];
        if (row_id < m_num_qrows_per_bank){
          m_translation->reserve("AQUA", addr);
        }
      }
    }
};

}       // namespace Ramulator
