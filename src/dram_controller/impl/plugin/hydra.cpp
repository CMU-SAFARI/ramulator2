#include <vector>
#include <unordered_map>
#include <limits>
#include <bitset>
#include <iomanip>
#include <random>

#include "base/base.h"
#include "frontend/frontend.h"
#include "translation/translation.h"
#include "addr_mapper/addr_mapper.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class Hydra : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, Hydra, "Hydra", "Hydra")

  private:
    IDRAM* m_dram = nullptr;
    ITranslation* m_translation = nullptr;
    IAddrMapper* m_addr_mapper = nullptr;

    struct GCT_Entry {
      int group_count;
      bool initialized;
    };

    int m_clk = -1;

    // input parameters
    int m_tracking_threshold = -1;
    int m_group_threshold = -1;
    int m_row_group_size = -1;
    int m_reset_period_ns = -1;
    int m_rcc_num_per_rank = -1;
    std::string m_rcc_policy = "RANDOM";

    int m_reset_period_clk = -1;

    int m_VRR_req_id = -1;
    int m_RD_req_id = -1;
    int m_WR_req_id = -1;

    int m_rank_level = -1;
    int m_bank_group_level = -1;
    int m_bank_level = -1;
    int m_row_level = -1;
    int m_col_level = -1;

    int m_num_ranks = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;
    int m_num_cls = -1;

    int m_row_address_bits = -1;
    int m_bank_address_bits = -1;
    int m_counter_bits = -1;
    int m_gct_entries_per_bank = -1;
    int m_gct_index_bits = -1;
    int m_rcc_set_num = -1;
    int m_rcc_index_bits = -1;
    int m_rcc_tag_row_bits = -1;
    int m_rcc_tag_bits = -1;

    int m_total_rct_cl_size = -1;
    int m_total_rct_row_size = -1;
    int m_rct_per_row = -1;
    int m_rct_per_cl = -1;
    int m_group_rct_cl_size = -1;

    // per bank GCT, 
    // the first index is the flat bank id
    // the second index is the row group id
    // each entry has a group counter and a flag indicating if the group counter has beed initialized
    // the row group id uses the most significant bits of the row id
    std::vector<std::unordered_map<Addr_t, GCT_Entry>> group_count_table;
    // per bank RCT,
    // the first index is the flat bank id
    // the second index is the row id
    // each entry has a row counter
    std::vector<std::unordered_map<Addr_t, int>> row_count_table;
    // per rank RCC,
    // a 16-set associative cache
    // the first index is the rank id
    // the second index is the rcc set id
    // each entry has an rcc tag and a row counter
    // the rcc set id uses the least significant bits of the row id
    // the rcc tag uses the most significant bits of the row id and the bank id
    std::vector<std::vector<std::unordered_map<Addr_t, int>>> row_count_cache;
    // per bank RCT count table,
    // the first index is the flat bank id
    // the second index is the row id
    // each entry has a row counter
    std::vector<std::unordered_map<Addr_t, int>> rct_count_table;

    // rng for random policy
    std::mt19937 generator;
    std::uniform_int_distribution<int> distribution;

    // stats
    int s_num_vrr = 0;
    int s_num_vrr_rct = 0;
    int s_num_read_req = 0;
    int s_num_write_req = 0;
    int s_num_initialization = 0;
    int s_num_eviction = 0;
    int s_num_rcc_miss = 0;
    int s_gct_check = 0;
    int s_rcc_check = 0;
    int s_rct_check = 0;
    int s_rctct_check = 0;

    bool m_is_debug;

  public:
    void init() override { 
      m_tracking_threshold = param<int>("hydra_tracking_threshold").required();
      m_group_threshold = param<int>("hydra_group_threshold").required();
      m_row_group_size = param<int>("hydra_row_group_size").default_val(128);
      m_reset_period_ns = param<int>("hydra_reset_period_ns").default_val(64000000);
      m_rcc_num_per_rank = param<int>("hydra_rcc_num_per_rank").default_val(4096);
      m_rcc_policy = param<std::string>("hydra_rcc_policy").default_val("RANDOM");
      m_is_debug = param<bool>("debug").default_val(false);
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_ctrl = cast_parent<IDRAMController>();
      m_dram = m_ctrl->m_dram;

      m_translation = frontend->get_ifce<ITranslation>();
      m_addr_mapper = memory_system->get_ifce<IAddrMapper>();

      if (!m_dram->m_commands.contains("VRR")) {
        throw ConfigurationError("Hydra is not compatible with the DRAM implementation that does not have Victim-Row-Refresh (VRR) command!");
      }

      m_reset_period_clk = m_reset_period_ns / ((float) m_dram->m_timing_vals("tCK_ps") / 1000.0f);

      m_VRR_req_id = m_dram->m_requests("victim-row-refresh");
      m_RD_req_id = m_dram->m_requests("read");
      m_WR_req_id = m_dram->m_requests("write");

      m_rank_level = m_dram->m_levels("rank");
      m_bank_group_level = m_dram->m_levels("bankgroup");
      m_bank_level = m_dram->m_levels("bank");
      m_row_level = m_dram->m_levels("row");
      m_col_level = m_dram->m_levels("column");

      m_num_ranks = m_dram->get_level_size("rank");
      m_num_banks_per_rank = m_dram->get_level_size("bankgroup") == -1 ? 
                             m_dram->get_level_size("bank") : 
                             m_dram->get_level_size("bankgroup") * m_dram->get_level_size("bank");
      m_num_rows_per_bank = m_dram->get_level_size("row");
      m_num_cls = m_dram->get_level_size("column") / 8;

      m_row_address_bits = log2(m_num_rows_per_bank);
      m_bank_address_bits = log2(m_num_banks_per_rank);
      m_counter_bits = ceil(log2(m_tracking_threshold) / 8) * 8;
      m_gct_entries_per_bank = m_num_rows_per_bank / m_row_group_size;
      m_gct_index_bits = log2(m_gct_entries_per_bank);
      m_rcc_set_num = m_rcc_num_per_rank / 16;
      m_rcc_index_bits = log2(m_rcc_set_num);
      m_rcc_tag_row_bits = m_row_address_bits - m_rcc_index_bits;
      m_rcc_tag_bits = m_rcc_tag_row_bits + m_bank_address_bits;

      // how many cache lines are needed to store the RCT for a bank
      m_total_rct_cl_size = m_num_rows_per_bank * m_counter_bits / 512;
      // how many rows are needed to store the RCT for a bank
      m_total_rct_row_size = ceil((float) m_total_rct_cl_size / (float) m_num_cls);
      // how many rct entries can be stored in a row
      m_rct_per_row = m_num_cls * 512 / m_counter_bits;
      // how many rct entries can be stored in a cl
      m_rct_per_cl = 512 / m_counter_bits;
      // how many cache lines are needed to store the RCT for a row group
      m_group_rct_cl_size = m_row_group_size * m_counter_bits / 512;

      // Initialize tables
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        std::unordered_map<Addr_t, GCT_Entry> gct_bank;
        group_count_table.push_back(gct_bank);
      }

      for (int i = 0; i < m_num_ranks; i++) {
        std::vector<std::unordered_map<Addr_t, int>> rcc_rank;
        for (int j = 0 ; j < m_rcc_set_num; j++){
          std::unordered_map<Addr_t, int> rcc_set;
          rcc_rank.push_back(rcc_set);
        }
        row_count_cache.push_back(rcc_rank);
      }
      
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        std::unordered_map<Addr_t, int> row_count_table_bank;
        row_count_table.push_back(row_count_table_bank);
      }

      rct_count_table.resize(m_num_ranks * m_num_banks_per_rank);
      for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
        std::unordered_map<Addr_t, int> rctct_bank;
        rct_count_table.push_back(rctct_bank);
      }

      if (m_is_debug) {
        std::cout << "------------------------------------" << std::endl
                  << "Hydra: Initialized" << std::endl;
        std::cout << "num_ranks:                  " << m_num_ranks << std::endl;
        std::cout << "num_banks_per_rank:         " << m_num_banks_per_rank << std::endl;
        std::cout << "num_rows_per_bank:          " << m_num_rows_per_bank << std::endl;
        std::cout << "num_cls:                    " << m_num_cls << std::endl;
        std::cout << "m_tracking_threshold:       " << m_tracking_threshold << std::endl;
        std::cout << "m_group_threshold:          " << m_group_threshold << std::endl;
        std::cout << "m_row_group_size:           " << m_row_group_size << std::endl;
        std::cout << "m_reset_period_ns:          " << m_reset_period_ns << std::endl;
        std::cout << "m_rcc_num_per_rank:         " << m_rcc_num_per_rank << std::endl;
        std::cout << "m_rcc_policy:               " << m_rcc_policy << std::endl;

        std::cout << "m_row_address_bits:         " << m_row_address_bits << std::endl;
        std::cout << "m_bank_address_bits:        " << m_bank_address_bits << std::endl;
        std::cout << "m_counter_bits:             " << m_counter_bits << std::endl;
        std::cout << "m_rcc_index_bits:           " << m_rcc_index_bits << std::endl;
        std::cout << "m_rcc_set_num:              " << m_rcc_set_num << std::endl;
        std::cout << "m_rcc_tag_row_bits:         " << m_rcc_tag_row_bits << std::endl;
        std::cout << "m_rcc_tag_bits:             " << m_rcc_tag_bits << std::endl;
        std::cout << "m_gct_entries_per_bank:     " << m_gct_entries_per_bank << std::endl;
        std::cout << "m_gct_index_bits:           " << m_gct_index_bits << std::endl;
        std::cout << "m_total_rct_cl_size:        " << m_total_rct_cl_size << std::endl;
        std::cout << "m_total_rct_row_size:       " << m_total_rct_row_size << std::endl;
        std::cout << "m_rct_per_row:              " << m_rct_per_row << std::endl;
        std::cout << "m_rct_per_cl:               " << m_rct_per_cl << std::endl;
        std::cout << "m_group_rct_cl_size:        " << m_group_rct_cl_size << std::endl;
      }

      reserve_rows_for_rct();

      register_stat(s_num_vrr).name("hydra_num_vrr");
      register_stat(s_num_vrr_rct).name("hydra_num_vrr_rct");
      register_stat(s_num_read_req).name("hydra_num_read_req");
      register_stat(s_num_write_req).name("hydra_num_write_req");
      register_stat(s_num_initialization).name("hydra_num_initialization");
      register_stat(s_num_eviction).name("hydra_num_eviction");
      register_stat(s_num_rcc_miss).name("hydra_num_rcc_miss");
      register_stat(s_gct_check).name("hydra_gct_check");
      register_stat(s_rcc_check).name("hydra_rcc_check");
      register_stat(s_rct_check).name("hydra_rct_check");
      register_stat(s_rctct_check).name("hydra_rctct_check");

      // setup random number generator for random policy
      generator = std::mt19937(1337);
      distribution = std::uniform_int_distribution<int>(0, 15);
    };

    void update(bool request_found, ReqBuffer::iterator& req_it) override {

      m_clk++;
      if (m_clk % m_reset_period_clk == 0) {
        for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
          group_count_table[i].clear();
        }
        for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
          row_count_table[i].clear();
        }
        for (int i = 0; i < m_num_ranks; i++) {
          for (int j = 0 ; j < m_rcc_set_num; j++){
            row_count_cache[i][j].clear();
          }
        }
        for (int i = 0; i < m_num_ranks * m_num_banks_per_rank; i++) {
          rct_count_table[i].clear();
        }
        if (m_is_debug) {
          std::cout << "----------------------------------" << std::endl;
          std::cout << "Hydra: Reset all tables (" << m_clk << ")" << std::endl;
        }
      }

      if (request_found){
        if (m_dram->m_command_meta(req_it->command).is_opening && m_dram->m_command_scopes(req_it->command) == m_row_level){
          int flat_bank_id = req_it->addr_vec[m_bank_level];
          int accumulated_dimension = 1;
          for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
            accumulated_dimension *= m_dram->m_organization.count[i + 1];
            flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
          }
          
          uint rank_id = req_it->addr_vec[m_rank_level];
          uint bank_id = flat_bank_id % m_num_banks_per_rank;
          uint row_id = req_it->addr_vec[m_row_level];
          uint gct_index = row_id >> (m_row_address_bits - m_gct_index_bits); // get most significant bits
          uint rcc_index = row_id & ((1 << m_rcc_index_bits) - 1); // get least significant bits
          uint rcc_tag = row_id >> (m_row_address_bits - m_rcc_tag_row_bits) // most significant bits of row_id 
                          | bank_id << m_rcc_tag_row_bits; // bank_id

          if (m_is_debug) {
            std::cout << "----------------------------------" << std::endl
                      << "Hydra: Activation cmd (" << m_clk << ") " << flat_bank_id << "," << gct_index << "," << row_id << std::endl
                      << "        flat_bank_id: " << std::setw(6) << flat_bank_id << " - " << std::bitset<5>(flat_bank_id) << std::endl
                      << "        rank_id:      " << std::setw(6) << rank_id      << " - " << std::bitset<1>(rank_id) << std::endl
                      << "        bank_id:      " << std::setw(6) << bank_id      << " -  " << std::bitset<4>(bank_id) << std::endl
                      << "        row_id:       " << std::setw(6) << row_id       << " -      " << std::bitset<16>(row_id) << std::endl
                      << "        gct_index:    " << std::setw(6) << gct_index    << " -      " << std::bitset<9>(gct_index) << std::endl
                      << "        rcc_index:    " << std::setw(6) << rcc_index    << " -              " << std::bitset<8>(rcc_index) << std::endl
                      << "        rcc_tag:      " << std::setw(6) << rcc_tag      << " -  " << std::bitset<12>(rcc_tag) << std::endl;
          }

          // if the row is in the RCT rows, use RCT_count_table
          if (row_id < m_total_rct_row_size){
            // increment RCT_count_table
            if (rct_count_table[flat_bank_id].find(row_id) == rct_count_table[flat_bank_id].end()){
              rct_count_table[flat_bank_id][row_id] = 0;
            }
            rct_count_table[flat_bank_id][row_id]++;
            if (m_is_debug) {
              std::cout << "Hydra: Row in RCT rows" << std::endl;
              std::cout << "Hydra: RCT_count_table incremented (" << rct_count_table[flat_bank_id][row_id] << ")" << std::endl;
            }
            // check rct_count_table
            s_rctct_check++;
            if (rct_count_table[flat_bank_id][row_id] >= m_tracking_threshold){
              if (m_is_debug) {
                std::cout << "Hydra: RCT_count_table above threshold, issue VRR, reset counter" << std::endl;
              }
              // issue VRR
              Request vrr_req(req_it->addr_vec, m_VRR_req_id);
              m_ctrl->priority_send(vrr_req);
              s_num_vrr_rct++;
              s_num_vrr++;
              // reset rcc
              rct_count_table[flat_bank_id].erase(row_id);
            } else {
              if (m_is_debug) {
                std::cout << "Hydra: RCT_count_table below threshold, do nothing" << std::endl;
              }
            }
            return;
          }

          // check gct
          s_gct_check++;

          if (group_count_table[flat_bank_id].find(gct_index) == group_count_table[flat_bank_id].end()) {
            GCT_Entry new_group_entry;
            new_group_entry.group_count = 0;
            new_group_entry.initialized = false;
            group_count_table[flat_bank_id][gct_index] = new_group_entry;
          }

          if (group_count_table[flat_bank_id][gct_index].group_count >= m_group_threshold){
            if (m_is_debug) {
              std::cout << "Hydra: Checking GCT" << std::endl;
              std::cout << "Hydra: GCT above threshold " 
                        << group_count_table[flat_bank_id][gct_index].group_count << std::endl;
            }

            if (!group_count_table[flat_bank_id][gct_index].initialized){
              if (m_is_debug) {
                std::cout << "Hydra: Group not initialized" << std::endl;
              }

              // initialize rct
              group_count_table[flat_bank_id][gct_index].initialized = true;
              s_num_initialization++;
              int row_group_start_row_id = gct_index * m_row_group_size;
              for (int i = 0; i < m_row_group_size; i++){
                int row = row_group_start_row_id + i;
                row_count_table[flat_bank_id][row] = m_group_threshold;
              }
              // generate write request to DRAM for rct
              for (int i = 0; i < m_group_rct_cl_size; i++){
                std::vector<int> rct_init_addr_vec;
                for (int j = 0; j < req_it->addr_vec.size(); j++){
                  rct_init_addr_vec.push_back(req_it->addr_vec[j]);
                }
                std::pair<Addr_t, Addr_t> init_row_col_id = generate_row_col_id(row_group_start_row_id + i * m_rct_per_cl);
                rct_init_addr_vec[m_row_level] = init_row_col_id.first;
                rct_init_addr_vec[m_col_level] = init_row_col_id.second;
                Request rct_init_req(rct_init_addr_vec, m_WR_req_id);
                m_ctrl->priority_send(rct_init_req);
                s_num_write_req++;
                
                if (m_is_debug) {
                  std::cout << "Hydra: Group initializing, generating write request to DRAM for RCT" << std::endl
                            << "        rct_bank: " << flat_bank_id << std::endl
                            << "        rct_row:  " << rct_init_addr_vec[m_row_level] << std::endl
                            << "        rct_col:  " << rct_init_addr_vec[m_col_level] << std::endl;
                }
              }
            } else {
              if (m_is_debug) {
                std::cout << "Hydra: Group already initialized" << std::endl;
              }
            }

            if (m_is_debug) {
              std::cout << "Hydra: Checking RCC[" << rank_id << "][" << rcc_index << "].size() = " << row_count_cache[rank_id][rcc_index].size() << std::endl;
              for (auto it = row_count_cache[rank_id][rcc_index].begin(); it != row_count_cache[rank_id][rcc_index].end(); it++){
                std::cout << "        tag: " << std::setw(6) << it->first << " counter: " << it->second << std::endl;
              }
            }

            // check rcc
            s_rcc_check++;
            if (row_count_cache[rank_id][rcc_index].find(rcc_tag) == row_count_cache[rank_id][rcc_index].end()){
              s_num_rcc_miss++;
              if (m_is_debug) {
                std::cout << "Hydra: RCC miss" << std::endl;
              }
              // check if rcc line is full
              if (row_count_cache[rank_id][rcc_index].size() == 16){
                // evicting an entry
                int tag_to_evict = get_tag_to_evict(rank_id, rcc_index);
                row_count_cache[rank_id][rcc_index].erase(tag_to_evict);
                if (m_is_debug) {
                  std::cout << "Hydra: RCC full, evicting " << tag_to_evict << std::endl;
                }
                // generate write request to DRAM for evicted entry
                std::vector<int> evicted_entry_addr_vec;
                for (int i = 0; i < req_it->addr_vec.size(); i++){
                  evicted_entry_addr_vec.push_back(req_it->addr_vec[i]);
                }
                int evicted_row_id = (tag_to_evict & ((1 << m_rcc_tag_row_bits) - 1)) << m_rcc_index_bits | rcc_index;
                int evicted_bank_id = tag_to_evict >> m_rcc_tag_row_bits;
                std::pair<Addr_t, Addr_t> evicted_row_col_id = generate_row_col_id(evicted_row_id);
                evicted_entry_addr_vec[m_bank_group_level] = evicted_bank_id / m_dram->get_level_size("bank");
                evicted_entry_addr_vec[m_bank_level] = evicted_bank_id % m_dram->get_level_size("bank");
                evicted_entry_addr_vec[m_row_level] = evicted_row_col_id.first;
                evicted_entry_addr_vec[m_col_level] = evicted_row_col_id.second;
                Request rct_write_req(evicted_entry_addr_vec, m_WR_req_id);
                m_ctrl->priority_send(rct_write_req);
                s_num_eviction++;
                s_num_write_req++;

                if (m_is_debug) {
                  std::cout << "Hydra: Generating write request to DRAM for evicted entry" << std::endl
                            << "        evicted_row_id:  " << std::setw(6) << evicted_row_id  << " -     " << std::bitset<16>(evicted_row_id) << std::endl
                            << "        evicted_bank_id: " << std::setw(6) << evicted_bank_id << " - " << std::bitset<4>(evicted_bank_id) << std::endl
                            << "        evicted_tag:     " << std::setw(6) << tag_to_evict    << " - " << std::bitset<12>(tag_to_evict) << std::endl
                            << "        rct_bank:        " << std::setw(6) << evicted_bank_id << std::endl
                            << "        rct_row:         " << std::setw(6) << evicted_entry_addr_vec[m_row_level] << std::endl
                            << "        rct_col:         " << std::setw(6) << evicted_entry_addr_vec[m_col_level] << std::endl;
                }
              } else {
                if (m_is_debug) {
                  std::cout << "Hydra: RCC not full" << std::endl;
                }
              }
              // read rct from DRAM and update rcc
              s_rct_check++;
              // copy addr_vec and update row_id
              AddrVec_t rct_read_addr_vec;
              for (int i = 0; i < req_it->addr_vec.size(); i++){
                rct_read_addr_vec.push_back(req_it->addr_vec[i]);
              }
              std::pair<Addr_t, Addr_t> row_col_id = generate_row_col_id(rct_read_addr_vec[m_row_level]);
              rct_read_addr_vec[m_row_level] = row_col_id.first;
              rct_read_addr_vec[m_col_level] = row_col_id.second;

              Request rct_read_req(rct_read_addr_vec, m_RD_req_id);
              m_ctrl->priority_send(rct_read_req);
              s_num_read_req++;

              // insert new entry and increment rcc
              row_count_table[flat_bank_id][row_id]++;
              row_count_cache[rank_id][rcc_index][rcc_tag] = row_count_table[flat_bank_id][row_id];
              
              if (m_is_debug) {
                std::cout << "Hydra: Generating read request to DRAM for RCT" << std::endl
                          << "        rct_bank: " << flat_bank_id << std::endl
                          << "        rct_row:  " << rct_read_addr_vec[m_row_level] << std::endl
                          << "        rct_col:  " << rct_read_addr_vec[m_col_level] << std::endl;
                std::cout << "Hydra: RCC incrementing" << std::endl;
              }
            } else {
              row_count_cache[rank_id][rcc_index][rcc_tag]++;
              row_count_table[flat_bank_id][row_id]++;
              if (m_is_debug) {
                std::cout << "Hydra: RCC hit" << std::endl;
                std::cout << "Hydra: RCC incrementing" << std::endl;
              }
            }

            if (m_is_debug) {
              std::cout << "Hydra: Checking RCC counter (" << row_count_cache[rank_id][rcc_index][rcc_tag] << ")" << std::endl;
            }

            // check if counter is above threshold
            if (row_count_cache[rank_id][rcc_index][rcc_tag] >= m_tracking_threshold){
              if (m_is_debug) {
                std::cout << "Hydra: RCC above threshold, issue VRR, reset counter" << std::endl;
              }
              // issue VRR
              Request vrr_req(req_it->addr_vec, m_VRR_req_id);
              m_ctrl->priority_send(vrr_req);
              s_num_vrr++;
              // reset rcc
              row_count_cache[rank_id][rcc_index][rcc_tag] = 0;
              row_count_table[flat_bank_id][row_id] = 0;
            } else {
              if (m_is_debug) {
                std::cout << "Hydra: RCC below threshold, do nothing" << std::endl;
              }
            }
          }
          else{
            if (m_is_debug) {
              std::cout << "Hydra: Checking GCT" << std::endl;
              std::cout << "Hydra: GCT below threshold (" << group_count_table[flat_bank_id][gct_index].group_count << ")" << std::endl;
              std::cout << "Hydra: GCT incrementing" << std::endl;
            }
            group_count_table[flat_bank_id][gct_index].group_count++;
          }
        }
      }
    };

    std::pair<Addr_t, Addr_t> generate_row_col_id(int row_id) {
      Addr_t rct_row_id = row_id / m_rct_per_row;
      Addr_t rct_col_id = (row_id % m_rct_per_row) * m_counter_bits / 512;
      rct_col_id = rct_col_id << 3;
      return std::make_pair(rct_row_id, rct_col_id);
    };

    int get_tag_to_evict(int rank_id, int rcc_index) {
      int tag_to_evict = -1;

      if (m_rcc_policy == "RANDOM") {
        int tag_index = distribution(generator);
        auto it = row_count_cache[rank_id][rcc_index].begin();
        std::advance(it, tag_index);
        tag_to_evict = it->first;
      } else if (m_rcc_policy == "MIN_COUNT") {
        int min_count = INT_MAX;
        for (auto it = row_count_cache[rank_id][rcc_index].begin(); it != row_count_cache[rank_id][rcc_index].end(); it++) {
          if (it->second < min_count) {
            min_count = it->second;
            tag_to_evict = it->first;
          }
        }
      } else {
        throw ConfigurationError("Undefined RCC eviction policy.");
      }

      return tag_to_evict;
    };

    void reserve_rows_for_rct() {
      Addr_t max_addr = m_translation->get_max_addr();
      // traverse all cls and reserve them if they use rows that store RCT
      Request req(0, 0);
      for (Addr_t addr = 0; addr < max_addr; addr += 64) {
        // apply address mapping
        req.addr = addr;
        m_addr_mapper->apply(req);
        Addr_t row_id = req.addr_vec[m_row_level];
        if (row_id < m_total_rct_row_size){
          m_translation->reserve("Hydra", addr);
        }
      }
    };
};

}       // namespace Ramulator
