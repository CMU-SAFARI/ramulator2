#include <bitset>
#include <climits>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

// Hydra RowHammer mitigation
//
// Implements the full hierarchical tracking: GCT → RCC → RCT, plus an
// RCTCT (RCT-region counter table) that catches activations to the RCT
// rows themselves. The RCT is conceptually stored in DRAM rows
// reserved by the memory controller (via AddrMapperBase's reserved_rows_per_bank)
// on every RCC miss we issue a real DRAM ACT + RD/WR against the RCT's physical row
// and on every dirty-line eviction we issue a write-back.
// The counter values still live in a software shadow map but the bank
// occupancy and bandwidth cost are modeled through real priority-buffer
// Cmd requests.
//
// Configure AddrMapperBase.reserved_rows_per_bank ≥
// m_total_rct_row_size (≈ num_rows * counter_bits / (512 * num_cls)).
// If reservation is smaller, workload requests can hit RCT rows and
// Hydra's own counters would be corrupted.
class Hydra : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, Hydra, "Hydra")

 private:
  ControllerBase* m_ctrl = nullptr;

  int m_clk = 0;

  // Parameters
  int m_tracking_threshold = -1;
  int m_group_threshold = -1;
  int m_row_group_size = -1;
  int m_reset_period_ns = -1;
  int m_rcc_num_per_rank = -1;
  std::string m_rcc_policy;
  bool m_is_debug = false;

  int m_reset_period_clk = -1;

  int m_vrr_cmd_id = -1;
  int m_act_cmd_id = -1;
  int m_rd_cmd_id = -1;
  int m_wr_cmd_id = -1;
  int m_rank_level = -1;
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

  // Group Count Table: per-bank, indexed by row group ID
  struct GCTEntry {
    int group_count = 0;
    bool initialized = false;
  };
  std::vector<std::unordered_map<int, GCTEntry>> m_gct;

  // Software shadow of the RCT (per bank, indexed by workload row ID).
  std::vector<std::unordered_map<int, int>> m_rct;

  // RCTCT — counters for ACTs that target the RCT region itself.
  // Hydra's own bookkeeping ACTs hammer RCT rows; their physical
  // neighbors hold workload data, so we track and fire VRR on threshold.
  std::vector<std::unordered_map<int, int>> m_rctct;

  // Row Count Cache: per-rank, 16-way set-associative.
  // Each entry carries the originating request's addr_vec so that, on
  // dirty-eviction, we can issue the RCT write-back to the correct bank.
  struct RCCEntry {
    int row_id = -1;
    int flat_bank_id = -1;
    int count = 0;
    bool dirty = false;
    AddrVec_t addr_template;
  };
  std::vector<std::vector<std::unordered_map<int, RCCEntry>>> m_rcc;

  // RNG for random eviction policy
  std::mt19937 m_generator;
  std::uniform_int_distribution<int> m_rcc_evict_dist;

  // Stats
  size_t s_num_vrr = 0;
  size_t s_num_vrr_rct = 0;
  size_t s_num_read_req = 0;
  size_t s_num_write_req = 0;
  size_t s_num_initialization = 0;
  size_t s_num_eviction = 0;
  size_t s_num_rcc_miss = 0;
  size_t s_gct_check = 0;
  size_t s_rcc_check = 0;
  size_t s_rct_check = 0;
  size_t s_rctct_check = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_tracking_threshold, int, "hydra_tracking_threshold").required();
    RAMULATOR_PARSE_PARAM(m_group_threshold, int, "hydra_group_threshold").required();
    RAMULATOR_PARSE_PARAM(m_row_group_size, int, "hydra_row_group_size").default_val(128);
    RAMULATOR_PARSE_PARAM(m_reset_period_ns, int, "hydra_reset_period_ns").default_val(64000000);
    RAMULATOR_PARSE_PARAM(m_rcc_num_per_rank, int, "hydra_rcc_num_per_rank").default_val(4096);
    RAMULATOR_PARSE_PARAM(m_rcc_policy, std::string, "hydra_rcc_policy").default_val("RANDOM");
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "Hydra is not compatible with the DRAM standard that does not "
          "have Victim-Row-Refresh (VRR) command!");
    }

    m_vrr_cmd_id = spec->get_command_id("VRR");
    m_act_cmd_id = spec->get_command_id("ACT");
    m_rd_cmd_id = spec->get_command_id("RD");
    m_wr_cmd_id = spec->get_command_id("WR");
    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");
    m_col_level = spec->get_level_id("Column");

    m_num_ranks = spec->get_level_size("Rank");
    int num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_num_banks_per_rank = num_banks / m_num_ranks;
    m_num_rows_per_bank = spec->get_level_size("Row");
    m_num_cls = spec->get_level_size("Column") / 8;

    m_reset_period_clk = m_reset_period_ns / (spec->get_timing_value("tCK_ps") / 1000.0f);

    // Derived sizing — matches upstream's formulas.
    m_row_address_bits = std::log2(m_num_rows_per_bank);
    m_bank_address_bits = std::log2(m_num_banks_per_rank);
    m_counter_bits = std::ceil(std::log2(m_tracking_threshold) / 8.0f) * 8;
    m_gct_entries_per_bank = m_num_rows_per_bank / m_row_group_size;
    m_gct_index_bits = std::log2(m_gct_entries_per_bank);
    m_rcc_set_num = m_rcc_num_per_rank / 16;
    m_rcc_index_bits = std::log2(m_rcc_set_num);
    m_rcc_tag_row_bits = m_row_address_bits - m_rcc_index_bits;
    m_rcc_tag_bits = m_rcc_tag_row_bits + m_bank_address_bits;

    m_total_rct_cl_size = m_num_rows_per_bank * m_counter_bits / 512;
    m_total_rct_row_size = std::ceil((float)m_total_rct_cl_size / (float)m_num_cls);
    m_rct_per_row = m_num_cls * 512 / m_counter_bits;
    m_rct_per_cl = 512 / m_counter_bits;
    m_group_rct_cl_size = m_row_group_size * m_counter_bits / 512;

    // Initialize tables
    m_gct.resize(num_banks);
    m_rct.resize(num_banks);
    m_rctct.resize(num_banks);

    m_rcc.resize(m_num_ranks);
    for (int i = 0; i < m_num_ranks; i++) {
      m_rcc[i].resize(m_rcc_set_num);
    }

    m_generator = std::mt19937(1337);
    m_rcc_evict_dist = std::uniform_int_distribution<int>(0, 15);

    m_stats.add("hydra_num_vrr", s_num_vrr);
    m_stats.add("hydra_num_vrr_rct", s_num_vrr_rct);
    m_stats.add("hydra_num_read_req", s_num_read_req);
    m_stats.add("hydra_num_write_req", s_num_write_req);
    m_stats.add("hydra_num_initialization", s_num_initialization);
    m_stats.add("hydra_num_eviction", s_num_eviction);
    m_stats.add("hydra_num_rcc_miss", s_num_rcc_miss);
    m_stats.add("hydra_gct_check", s_gct_check);
    m_stats.add("hydra_rcc_check", s_rcc_check);
    m_stats.add("hydra_rct_check", s_rct_check);
    m_stats.add("hydra_rctct_check", s_rctct_check);

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
  }

  void pre_schedule() override {
    m_clk++;

    if (m_clk % m_reset_period_clk == 0) {
      for (auto& t : m_gct) t.clear();
      for (auto& t : m_rct) t.clear();
      for (auto& t : m_rctct) t.clear();
      for (auto& rank : m_rcc) {
        for (auto& set : rank) {
          set.clear();
        }
      }
      if (m_is_debug) {
        std::cout << "----------------------------------" << std::endl;
        std::cout << "Hydra: Reset all tables (" << m_clk << ")" << std::endl;
      }
    }
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->command_meta[req.command].is_opening) return;
    if (spec->bank_targets[req.command] != BankTarget::Single) return;

    int row_id = req.addr_vec[m_row_level];
    int flat_bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    int rank_id = req.addr_vec[m_rank_level];
    int bank_id = flat_bank_id % m_num_banks_per_rank;

    int gct_index = row_id >> (m_row_address_bits - m_gct_index_bits);
    int rcc_index = row_id & ((1 << m_rcc_index_bits) - 1);
    int rcc_tag = (row_id >> (m_row_address_bits - m_rcc_tag_row_bits))
                  | (bank_id << m_rcc_tag_row_bits);

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

    // ── RCTCT branch: ACT targets a row inside the RCT region ──────
    // Hydra's own bookkeeping traffic hits these rows. Track and fire
    // VRR if the count crosses threshold to defend their workload-
    // neighbor rows from indirect RowHammer.
    if (row_id < m_total_rct_row_size) {
      if (m_rctct[flat_bank_id].find(row_id) == m_rctct[flat_bank_id].end()) {
        m_rctct[flat_bank_id][row_id] = 0;
      }
      m_rctct[flat_bank_id][row_id]++;
      if (m_is_debug) {
        std::cout << "Hydra: Row in RCT rows" << std::endl;
        std::cout << "Hydra: RCT_count_table incremented (" << m_rctct[flat_bank_id][row_id] << ")" << std::endl;
      }
      s_rctct_check++;
      if (m_rctct[flat_bank_id][row_id] >= m_tracking_threshold) {
        if (m_is_debug) {
          std::cout << "Hydra: RCT_count_table above threshold, issue VRR, reset counter" << std::endl;
        }
        Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
        m_ctrl->priority_send(vrr_req);
        s_num_vrr_rct++;
        s_num_vrr++;
        m_rctct[flat_bank_id].erase(row_id);
      } else {
        if (m_is_debug) {
          std::cout << "Hydra: RCT_count_table below threshold, do nothing" << std::endl;
        }
      }
      return;
    }

    // ── Step 1: Check GCT ──────────────────────────────────────────
    s_gct_check++;
    auto& gct_entry = m_gct[flat_bank_id][gct_index];

    if (gct_entry.group_count < m_group_threshold) {
      if (m_is_debug) {
        std::cout << "Hydra: Checking GCT" << std::endl;
        std::cout << "Hydra: GCT below threshold (" << gct_entry.group_count << ")" << std::endl;
        std::cout << "Hydra: GCT incrementing" << std::endl;
      }
      gct_entry.group_count++;
      return;
    }

    if (m_is_debug) {
      std::cout << "Hydra: Checking GCT" << std::endl;
      std::cout << "Hydra: GCT above threshold "
                << gct_entry.group_count << std::endl;
    }

    // ── Step 2: Initialize RCT for this group if needed ───────────
    if (!gct_entry.initialized) {
      if (m_is_debug) {
        std::cout << "Hydra: Group not initialized" << std::endl;
      }
      gct_entry.initialized = true;
      s_num_initialization++;
      int row_group_start = gct_index * m_row_group_size;
      for (int i = 0; i < m_row_group_size; i++) {
        m_rct[flat_bank_id][row_group_start + i] = m_group_threshold;
      }
      // Generate RCT-init writes to DRAM (one per cache line in the
      // group's RCT footprint).
      for (int i = 0; i < m_group_rct_cl_size; i++) {
        int cl_workload_row = row_group_start + i * m_rct_per_cl;
        issue_rct_access(/*is_write=*/true, cl_workload_row, req.addr_vec);
        if (m_is_debug) {
          std::cout << "Hydra: Group initializing, generating write request to DRAM for RCT" << std::endl
                    << "        rct_bank: " << flat_bank_id << std::endl
                    << "        rct_row:  " << rct_row_for(cl_workload_row) << std::endl;
        }
      }
    } else {
      if (m_is_debug) {
        std::cout << "Hydra: Group already initialized" << std::endl;
      }
    }

    if (m_is_debug) {
      std::cout << "Hydra: Checking RCC[" << rank_id << "][" << rcc_index << "].size() = "
                << m_rcc[rank_id][rcc_index].size() << std::endl;
      for (auto& [tag, entry] : m_rcc[rank_id][rcc_index]) {
        std::cout << "        tag: " << std::setw(6) << tag << " counter: " << entry.count << std::endl;
      }
    }

    // ── Step 3: Check RCC ──────────────────────────────────────────
    s_rcc_check++;
    auto& rcc_set = m_rcc[rank_id][rcc_index];
    auto it = rcc_set.find(rcc_tag);
    if (it == rcc_set.end()) {
      s_num_rcc_miss++;
      if (m_is_debug) {
        std::cout << "Hydra: RCC miss" << std::endl;
      }
      // Evict if set is full (16-way). A dirty victim triggers a
      // write-back to its RCT row on the evictee's bank.
      if (rcc_set.size() == 16) {
        int tag_to_evict = get_tag_to_evict(rank_id, rcc_index);
        if (m_is_debug) {
          std::cout << "Hydra: RCC full, evicting " << tag_to_evict << std::endl;
        }
        auto evict_it = rcc_set.find(tag_to_evict);
        if (evict_it != rcc_set.end()) {
          int evicted_row_id = evict_it->second.row_id;
          int evicted_bank_id = evict_it->second.flat_bank_id % m_num_banks_per_rank;
          issue_rct_access(/*is_write=*/true, evicted_row_id,
                           evict_it->second.addr_template);
          if (m_is_debug) {
            std::cout << "Hydra: Generating write request to DRAM for evicted entry" << std::endl
                      << "        evicted_row_id:  " << std::setw(6) << evicted_row_id << " -     " << std::bitset<16>(evicted_row_id) << std::endl
                      << "        evicted_bank_id: " << std::setw(6) << evicted_bank_id << " - " << std::bitset<4>(evicted_bank_id) << std::endl
                      << "        evicted_tag:     " << std::setw(6) << tag_to_evict << " - " << std::bitset<12>(tag_to_evict) << std::endl
                      << "        rct_bank:        " << std::setw(6) << evicted_bank_id << std::endl
                      << "        rct_row:         " << std::setw(6) << rct_row_for(evicted_row_id) << std::endl;
          }
          rcc_set.erase(evict_it);
          s_num_eviction++;
        }
      } else {
        if (m_is_debug) {
          std::cout << "Hydra: RCC not full" << std::endl;
        }
      }

      // Read RCT from DRAM and install in RCC.
      s_rct_check++;
      issue_rct_access(/*is_write=*/false, row_id, req.addr_vec);
      if (m_is_debug) {
        std::cout << "Hydra: Generating read request to DRAM for RCT" << std::endl
                  << "        rct_bank: " << flat_bank_id << std::endl
                  << "        rct_row:  " << rct_row_for(row_id) << std::endl;
        std::cout << "Hydra: RCC incrementing" << std::endl;
      }

      m_rct[flat_bank_id][row_id]++;
      rcc_set[rcc_tag] = RCCEntry{row_id, flat_bank_id,
                                  m_rct[flat_bank_id][row_id],
                                  /*dirty=*/true, req.addr_vec};
      it = rcc_set.find(rcc_tag);
    } else {
      // RCC hit — increment in cache only.
      it->second.count++;
      it->second.dirty = true;
      m_rct[flat_bank_id][row_id]++;
      if (m_is_debug) {
        std::cout << "Hydra: RCC hit" << std::endl;
        std::cout << "Hydra: RCC incrementing" << std::endl;
      }
    }

    if (m_is_debug) {
      std::cout << "Hydra: Checking RCC counter (" << it->second.count << ")" << std::endl;
    }

    // ── Step 4: Check threshold and issue VRR if needed ────────────
    if (it->second.count >= m_tracking_threshold) {
      if (m_is_debug) {
        std::cout << "Hydra: RCC above threshold, issue VRR, reset counter" << std::endl;
      }
      Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
      m_ctrl->priority_send(vrr_req);
      s_num_vrr++;
      it->second.count = 0;
      it->second.dirty = true;
      m_rct[flat_bank_id][row_id] = 0;
    } else {
      if (m_is_debug) {
        std::cout << "Hydra: RCC below threshold, do nothing" << std::endl;
      }
    }
  }

 private:
  // Map a logical workload row to the (rct_row, rct_col) pair that
  // stores its counter. Mirrors upstream's generate_row_col_id:
  // counters are packed m_rct_per_row per DRAM row; within each row
  // they're laid out at counter_bits granularity, addressed at column-
  // burst granularity (the << 3 converts cl-index to column index).
  std::pair<int, int> generate_row_col_id(int workload_row_id) const {
    int rct_row_id = workload_row_id / m_rct_per_row;
    int rct_col_id = (workload_row_id % m_rct_per_row) * m_counter_bits / 512;
    rct_col_id = rct_col_id << 3;
    return {rct_row_id, rct_col_id};
  }

  int rct_row_for(int workload_row_id) const {
    return workload_row_id / m_rct_per_row;
  }

  // Issue a priority ACT + RD/WR pair targeting the RCT row.
  void issue_rct_access(bool is_write, int workload_row_id,
                        const AddrVec_t& template_addr_vec) {
    auto [rct_row, rct_col] = generate_row_col_id(workload_row_id);
    AddrVec_t addr = template_addr_vec;
    addr[m_row_level] = rct_row;
    addr[m_col_level] = rct_col;

    Request act_req(addr, Request::Cmd, m_act_cmd_id);
    m_ctrl->priority_send(act_req);

    int col_cmd_id = is_write ? m_wr_cmd_id : m_rd_cmd_id;
    Request col_req(addr, Request::Cmd, col_cmd_id);
    m_ctrl->priority_send(col_req);

    if (is_write) {
      s_num_write_req++;
    } else {
      s_num_read_req++;
    }
  }

  int get_tag_to_evict(int rank_id, int rcc_index) {
    auto& rcc_set = m_rcc[rank_id][rcc_index];

    if (m_rcc_policy == "RANDOM") {
      int idx = m_rcc_evict_dist(m_generator);
      auto it = rcc_set.begin();
      std::advance(it, idx % rcc_set.size());
      return it->first;
    } else if (m_rcc_policy == "MIN_COUNT") {
      int min_count = INT_MAX;
      int min_tag = -1;
      for (auto& [tag, entry] : rcc_set) {
        if (entry.count < min_count) {
          min_count = entry.count;
          min_tag = tag;
        }
      }
      return min_tag;
    }
    throw std::runtime_error("Hydra: Unknown RCC eviction policy: " + m_rcc_policy);
  }
};

}  // namespace Ramulator
