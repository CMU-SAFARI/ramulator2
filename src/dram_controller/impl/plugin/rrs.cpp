#include <vector>
#include <unordered_map>
#include <limits>
#include <random>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "addr_mapper/impl/rit.h"

namespace Ramulator {

class RRS : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, RRS, "RRS", "Randomized Row-Swap.")

  private:
    IDRAM* m_dram = nullptr;
    LinearMapperBase_with_rit* m_addr_mapper = nullptr;

    int m_clk = 0;

    int m_num_hrt_entries = -1;
    int m_num_rit_entries = -1;
    int m_rss_threshold = -1;
    int m_reset_period_ns = -1;
    int m_reset_period_clk = -1;
    bool m_is_debug = false;
    
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
    std::vector<std::unordered_map<int, int>> m_hot_row_tracker;
    // spillover counter per bank
    std::vector<int> m_spillover_counter;
    // per bank row indirection table is implemented in 'src/addr_mapper/impl/linear_mappers_with_rit.cpp'
    
    // rng
    std::mt19937 generator;
    std::uniform_int_distribution<int> distribution;

    // statistics
    int s_num_swaps = 0;
    int s_num_unswaps = 0;
    int s_num_reswaps = 0;

  public:
    void init() override { 
      m_num_hrt_entries = param<int>("num_hrt_entries").required();
      m_num_rit_entries = param<int>("num_rit_entries").required();
      m_rss_threshold = param<int>("rss_threshold").required();
      m_reset_period_ns = param<int>("reset_period_ns").required();
      m_is_debug = param<bool>("debug").default_val(false);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;
      m_addr_mapper = (LinearMapperBase_with_rit*) memory_system->get_ifce<IAddrMapper>();

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
        m_hot_row_tracker.push_back(table);
      }
      // Initialize spillover counter
      m_spillover_counter = std::vector<int>(m_num_banks_per_rank * m_num_ranks, 0);
      // Initialize row indirection table in the addr_mapper
      m_addr_mapper->init_rit(m_num_banks_per_rank * m_num_ranks, m_num_rit_entries);
      
      // setup random number generator
      generator = std::mt19937(1337);
      distribution = std::uniform_int_distribution<int>(0, m_num_rows_per_bank);

      // Register statistics
      register_stat(s_num_swaps).name("rss_num_swaps");
      register_stat(s_num_unswaps).name("rss_num_unswaps");
      register_stat(s_num_reswaps).name("rss_num_reswaps");

      if (m_is_debug) {
        std::cout << "RRS is implemented." << std::endl
                  << "Number of HRT entries: " << m_num_hrt_entries << std::endl
                  << "Number of RIT entries: " << m_num_rit_entries << std::endl
                  << "RRS threshold: " << m_rss_threshold << std::endl
                  << "Number of rows: " << m_num_rows_per_bank << std::endl
                  << "Number of banks: " << m_num_ranks * m_num_banks_per_rank << std::endl
                  << "------------------------------------------------" << std::endl;
      }
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
      // Tick myself
      m_clk++;

      if (m_clk % m_reset_period_clk == 0) {
        // Reset hrt and unlock rit
        for (int i = 0; i < m_num_banks_per_rank * m_num_ranks; i++) {
          m_hot_row_tracker[i].clear();
          m_spillover_counter[i] = 0;
          m_addr_mapper->rit_unlock();
        }
        if (m_is_debug) {
          std::cout << "----------------------------" << std::endl;
          std::cout << "RRS is resetting. " << m_clk << std::endl;
          for (int b = 0; b < m_num_banks_per_rank * m_num_ranks; b++)
            m_addr_mapper->dump_rit(b);
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
            std::cout << "RRS: ACT on row " << row_id << "         " << m_clk << std::endl;
            std::cout << "  └  " << "bank: " << flat_bank_id << std::endl;
          }

          // Check HRT
          if (m_hot_row_tracker[flat_bank_id].find(row_id) == m_hot_row_tracker[flat_bank_id].end()) {
            if (m_is_debug) {
              std::cout << "  └  " << "row " << row_id << " not in HRT." << std::endl;
            }
            // if row is not in the table, check if the table is full 
            if (m_hot_row_tracker[flat_bank_id].size() < m_num_hrt_entries) {
              if (m_is_debug) {
                std::cout << "  └  " << "HRT is not full, inserting with count 1." << std::endl;
              }
              // if table is not full, insert the row
              m_hot_row_tracker[flat_bank_id][row_id] = 1;
            } else {
              if (m_is_debug) {
                std::cout << "  └  " << "HRT is full, searching for a row to evict." << std::endl;
              }
              // if table is full, find a row to evict
              bool found = false;
              int to_remove = -1;
              int spillover_value = -1;

              for (auto it = m_hot_row_tracker[flat_bank_id].begin(); it != m_hot_row_tracker[flat_bank_id].end(); it++) {
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
                m_hot_row_tracker[flat_bank_id].erase(to_remove);
                // add row_id to the table
                m_hot_row_tracker[flat_bank_id][row_id] = spillover_value + 1;
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
            m_hot_row_tracker[flat_bank_id][row_id] += 1;
          }
          // dump HRT for debug
          if (m_is_debug) {
            std::cout << "==========================" << std::endl;
            std::cout << "HRT[" << flat_bank_id << "].size(): " << m_hot_row_tracker[flat_bank_id].size() << std::endl;
            for (auto entry: m_hot_row_tracker[flat_bank_id]) {
              std::cout << entry.first << ":\t" << entry.second << std::endl; 
            }
            std::cout << "Spillover counter: " << m_spillover_counter[flat_bank_id] << std::endl;
            std::cout << "==========================" << std::endl;
          }

          // row is now in the table, check if the count exceeds the threshold
          if (m_is_debug) {
            std::cout << "Row " << row_id << " in HRT" << std::endl;
            std::cout << "  └  " << "threshold: " << m_rss_threshold << std::endl;
            std::cout << "  └  " << "count: " << m_hot_row_tracker[flat_bank_id][row_id] << std::endl;
          }
          if (m_hot_row_tracker[flat_bank_id][row_id] % m_rss_threshold == 0) {
            if (m_is_debug) {
              std::cout << "Row " << row_id << " needs swapping!" << std::endl;
            }
            // issue swap

            // check if the row is already swapped in the current epoch
            int prev_swapped_row = m_addr_mapper->check_rit(flat_bank_id, row_id);
            if (prev_swapped_row != -1) {
              if (m_addr_mapper->is_rit_locked(flat_bank_id, row_id)) {
                // we need to swap both of the rows.
                if (m_is_debug) {
                  std::cout << "Row " << row_id << " is already swapped with row " << prev_swapped_row << " in the current epoch." << std::endl;
                  std::cout << "We need to swap both rows." << std::endl;
                }
                // check if rit has empty slots
                if (m_addr_mapper->is_rit_full(flat_bank_id)) {
                  // if rit is full, get a pair to unswap
                  auto unswap_pair = m_addr_mapper->get_unswap_pair(flat_bank_id, m_hot_row_tracker[flat_bank_id]);
                  if (m_is_debug) {
                    std::cout << "RIT is full." << std::endl;
                    std::cout << "Unswapping row " << unswap_pair.first << " with row " << unswap_pair.second << std::endl;
                  }
                  // unswap the pair
                  issue_swap(req_it, unswap_pair.first, unswap_pair.second);
                  // remove the pair from the rit
                  m_addr_mapper->rit_remove_entry(flat_bank_id, unswap_pair.first, unswap_pair.second);

                  s_num_unswaps++;
                }
                // get 2 new rows 
                int dst_row0 = get_rand_row(flat_bank_id, row_id);
                int dst_row1 = get_rand_row(flat_bank_id, row_id);
                if (m_is_debug) {
                  std::cout << "Swapping row " << row_id << " with row " << dst_row0 << std::endl;
                  std::cout << "Swapping row " << prev_swapped_row << " with row " << dst_row1 << std::endl;
                }
                // remove the prev_swap entries
                m_addr_mapper->rit_remove_entry(flat_bank_id, row_id, prev_swapped_row);

                // issue new swaps
                issue_swap(req_it, row_id, dst_row0);
                issue_swap(req_it, prev_swapped_row, dst_row1);
              
                m_addr_mapper->rit_insert_entry(flat_bank_id, row_id, dst_row1);
                m_addr_mapper->rit_insert_entry(flat_bank_id, prev_swapped_row, dst_row0);

                s_num_swaps++;
                s_num_swaps++;
                s_num_reswaps++;
              } else {
                // we need to unswap and reswap the row
                // find a row to swap with
                int dst_row = get_rand_row(flat_bank_id, row_id);
                
                if (m_is_debug) {
                  std::cout << "Row " << row_id << " is already swapped with row " << prev_swapped_row << " in the previous epochs." << std::endl;
                  std::cout << "We need to unswap and reswap the row." << std::endl;
                  std::cout << "Unswapping row " << row_id << " with row " << prev_swapped_row << std::endl;
                  std::cout << "Swapping row " << row_id << " with row " << dst_row << std::endl;
                }
                
                // unswap the pair
                issue_swap(req_it, row_id, prev_swapped_row);
                // remove the pair from the rit
                m_addr_mapper->rit_remove_entry(flat_bank_id, row_id, prev_swapped_row);

                s_num_unswaps++;
                
                // swap the pair
                issue_swap(req_it, row_id, dst_row);
                // add the pair to the rit
                m_addr_mapper->rit_insert_entry(flat_bank_id, row_id, dst_row);

                s_num_swaps++;
              }
            } else {
              // we need to swap the row
              // check if rit has empty slots
              if (m_addr_mapper->is_rit_full(flat_bank_id)) {
                // if rit is full, get a pair to unswap
                auto unswap_pair = m_addr_mapper->get_unswap_pair(flat_bank_id, m_hot_row_tracker[flat_bank_id]);
                if (m_is_debug) {
                  std::cout << "RIT is full." << std::endl;
                  std::cout << "Unswapping row " << unswap_pair.first << " with row " << unswap_pair.second << std::endl;
                }
                // unswap the pair
                issue_swap(req_it, unswap_pair.first, unswap_pair.second);
                // remove the pair from the rit
                m_addr_mapper->rit_remove_entry(flat_bank_id, unswap_pair.first, unswap_pair.second);

                s_num_unswaps++;
              }

              // find a row to swap with
              int dst_row = get_rand_row(flat_bank_id, row_id);
              if (m_is_debug) {
                std::cout << "Swapping row " << row_id << " with row " << dst_row << std::endl;
              }
              // swap the pair
              issue_swap(req_it, row_id, dst_row);
              // add the pair to the rit
              m_addr_mapper->rit_insert_entry(flat_bank_id, row_id, dst_row);

              s_num_swaps++;
            }

            if (m_is_debug) {
              m_addr_mapper->dump_rit(flat_bank_id);
            }
          }
        }
      }
    }

    int get_rand_row(int bank_id, int row_id) {
      // find a row to swap with
      int dst_row = -1;
      while (dst_row == -1) {
        int rand_row = distribution(generator);
        // check if rand row is in hrt or is in rit or is not row_id 
        if (m_hot_row_tracker[bank_id].find(rand_row) == m_hot_row_tracker[bank_id].end() 
            && m_addr_mapper->check_rit(bank_id, rand_row) == -1
            && rand_row != row_id) {
          dst_row = rand_row;
        }
      }
      return dst_row;
    }

    void issue_swap(ReqBuffer::iterator& req_it, int src_row, int dst_row) {
      // load addr_vec
      std::vector<int> addr_vec;
      for (int i = 0; i < req_it->addr_vec.size(); i++){
        addr_vec.push_back(req_it->addr_vec[i]);
      }

      // Read src_row to buffer0 
      addr_vec[m_row_level] = src_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_read_req(addr_vec, m_RD_req_id);
        if (!m_ctrl->priority_send(swap_read_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }

      // Read dst_row to buffer1
      addr_vec[m_row_level] = dst_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_read_req(addr_vec, m_RD_req_id);
        if (!m_ctrl->priority_send(swap_read_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }

      // Write buffer0 to dst_row
      addr_vec[m_row_level] = dst_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_write_req(addr_vec, m_WR_req_id);
        if (!m_ctrl->priority_send(swap_write_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }

      // Write buffer1 to src_row
      addr_vec[m_row_level] = src_row;
      for (int cl = 0; cl < m_num_cls; cl++){
        addr_vec[m_col_level] = cl << 3;
        Request swap_write_req(addr_vec, m_WR_req_id);
        if (!m_ctrl->priority_send(swap_write_req)){
          std::cerr << "Check priority queue max size." << std::endl;
          exit(1);
        }
      }
    }

};

}       // namespace Ramulator
