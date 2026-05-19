#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/addr_mapper/impl/rit_addr_mapper.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

// AQUA (Aggressor-row QUArantine) — migrate hot rows into a reserved
// "quarantine" region at the low rows of each bank. A rotating head
// pointer (RQA head) picks successive quarantine slots, so each
// migration targets a distinct physical row and ages out older
// migrations as the head wraps around.
//
//   - RITAddrMapper handles the logical→physical indirection.
//   - AddrMapperBase.reserved_rows_per_bank carves the quarantine
//     region out of the workload's address space.
// The user must configure:
//   addr_mapper = RITAddrMapper(reserved_rows_per_bank=num_qrows_per_bank)
//   controller_plugins = [AQUA(num_qrows_per_bank=Q, ...)]
// If the addr-mapper reservation and AQUA's Q disagree, setup() throws.
//
class AQUA : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, AQUA, "AQUA")

 private:
  ControllerBase* m_ctrl = nullptr;
  RITAddrMapper* m_rit_mapper = nullptr;

  // Params
  int m_num_art_entries = -1;
  int m_num_fpt_entries = -1;
  int m_num_qrows_per_bank = -1;
  int m_art_threshold = -1;
  int m_reset_period_ns = -1;
  bool m_is_debug = false;

  int m_reset_period_clk = -1;
  int m_clk = 0;

  int m_rd_cmd_id = -1;
  int m_wr_cmd_id = -1;
  int m_row_level = -1;
  int m_col_level = -1;

  int m_num_banks = -1;
  int m_num_rows_per_bank = -1;
  int m_num_cls = -1;          // cache lines per row (= num_columns / prefetch)
  int m_prefetch = -1;         // burst size (column stride between cache lines)

  // Aggressor Row Tracker — per-bank ACT-count map with
  // spillover eviction. When the table fills past m_num_art_entries
  // and a new row arrives, evict an entry whose count equals the
  // current spillover counter; otherwise increment the spillover
  // counter so subsequent untracked rows can age out cold entries.
  std::vector<std::unordered_map<int, int>> m_art;
  std::vector<int> m_spillover_counter;

  // Reverse Pointer Table — per bank, maps a quarantine row back to the
  // workload row currently quarantined there. Needed to evict old
  // migrations when the RQA head wraps.
  std::vector<std::unordered_map<int, int>> m_rpt;

  // Rotating quarantine-head pointer (shared across banks).
  int m_rqa_head = 0;

  // Stats
  size_t s_num_migrations = 0;
  size_t s_num_r_migrations = 0;  // re-migrations

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_num_art_entries, int, "num_art_entries").required();
    RAMULATOR_PARSE_PARAM(m_num_fpt_entries, int, "num_fpt_entries").required();
    RAMULATOR_PARSE_PARAM(m_num_qrows_per_bank, int, "num_qrows_per_bank").required();
    RAMULATOR_PARSE_PARAM(m_art_threshold, int, "art_threshold").required();
    RAMULATOR_PARSE_PARAM(m_reset_period_ns, int, "reset_period_ns").default_val(64000000);
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    m_rit_mapper = dynamic_cast<RITAddrMapper*>(m_ctrl->m_addr_mapper);
    if (!m_rit_mapper) {
      throw std::runtime_error(
          "AQUA requires the controller's addr_mapper to be RITAddrMapper");
    }

    m_rd_cmd_id = spec->get_command_id("RD");
    m_wr_cmd_id = spec->get_command_id("WR");
    m_row_level = spec->get_level_id("Row");
    m_col_level = spec->get_level_id("Column");

    m_num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_num_rows_per_bank = spec->get_level_size("Row");
    m_prefetch = spec->internal_prefetch_size;
    m_num_cls = spec->get_level_size("Column") / m_prefetch;
    m_reset_period_clk = m_reset_period_ns / (spec->get_timing_value("tCK_ps") / 1000.0f);

    if (m_num_qrows_per_bank <= 0 || m_num_qrows_per_bank >= m_num_rows_per_bank) {
      throw std::runtime_error("AQUA: num_qrows_per_bank out of range");
    }

    m_art.resize(m_num_banks);
    m_spillover_counter.assign(m_num_banks, 0);
    m_rpt.resize(m_num_banks);

    // RIT capacity is set by num_fpt_entries
    m_rit_mapper->init_rit(m_num_banks, m_num_fpt_entries);

    m_stats.add("aqua_migrations", s_num_migrations);
    m_stats.add("aqua_r_migrations", s_num_r_migrations);
  }

  void pre_schedule() override {
    m_clk++;

    if (m_clk % m_reset_period_clk == 0) {
      for (auto& t : m_art) t.clear();
      std::fill(m_spillover_counter.begin(), m_spillover_counter.end(), 0);
      // RPT and RIT persist across epochs in AQUA (migrations are
      // "permanent" until the head overwrites them).
    }
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->command_meta[req.command].is_opening) return;
    if (spec->bank_targets[req.command] != BankTarget::Single) return;

    int bank = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    int row = req.addr_vec[m_row_level];

    if (m_is_debug) {
      std::cout << "----------------------------" << std::endl;
      std::cout << "AQUA: ACT on row " << row << "         " << m_clk << std::endl;
      std::cout << "  └  " << "bank: " << bank << std::endl;
    }

    auto it = m_art[bank].find(row);
    if (it == m_art[bank].end()) {
      if (m_is_debug) {
        std::cout << "  └  " << "row " << row << " not in HRT." << std::endl;
      }
      if ((int)m_art[bank].size() < m_num_art_entries) {
        if (m_is_debug) {
          std::cout << "  └  " << "HRT is not full, inserting with count 1." << std::endl;
        }
        m_art[bank][row] = 1;
      } else {
        if (m_is_debug) {
          std::cout << "  └  " << "HRT is full, searching for a row to evict." << std::endl;
        }
        bool found = false;
        int to_remove = -1, spillover_value = -1;
        for (auto& [r, c] : m_art[bank]) {
          if (c == m_spillover_counter[bank]) {
            if (m_is_debug) {
              std::cout << "  └  " << "found a row to evict: " << r << std::endl;
            }
            to_remove = r;
            spillover_value = c;
            found = true;
            break;
          }
        }
        if (found) {
          if (m_is_debug) {
            std::cout << "Removing row " << to_remove << " from HRT." << std::endl;
            std::cout << "Adding row " << row << " to HRT." << std::endl;
          }
          m_art[bank].erase(to_remove);
          m_art[bank][row] = spillover_value + 1;
        } else {
          if (m_is_debug) {
            std::cout << "  └  " << "no row to evict, incrementing spillover counter." << std::endl;
          }
          m_spillover_counter[bank]++;
          return;
        }
      }
    } else {
      if (m_is_debug) {
        std::cout << "  └  " << "row " << row << " in HRT. Incrementing its counter." << std::endl;
      }
      it->second++;
    }

    if (m_is_debug) {
      std::cout << "Row " << row << " in ART" << std::endl;
      std::cout << "  └  " << "threshold: " << m_art_threshold << std::endl;
      std::cout << "  └  " << "count: " << m_art[bank][row] << std::endl;
    }

    // Migrate only on multiples of the threshold.
    int count = m_art[bank][row];
    if (count % m_art_threshold != 0) return;

    int q_row = m_rqa_head;
    if (m_is_debug) {
      std::cout << "Row " << row << " needs quarantine!" << std::endl;
      std::cout << "  └  " << "RQA head: " << q_row << std::endl;
    }

    // If the head already holds someone, evict them first.
    auto prev_it = m_rpt[bank].find(q_row);
    if (prev_it != m_rpt[bank].end()) {
      int prev_workload_row = prev_it->second;
      if (m_is_debug) {
        std::cout << "RQA head is valid, evicting row " << q_row << std::endl;
      }
      m_rpt[bank].erase(q_row);
      m_rit_mapper->remove_entry(bank, prev_workload_row, q_row);
      issue_migration(req.addr_vec, q_row, prev_workload_row);
      s_num_r_migrations++;
    } else {
      if (m_is_debug) {
        std::cout << "RQA head is empty, issuing migration." << std::endl;
      }
    }

    // If `row` is itself a quarantine slot, fold the prior mapping under the new head
    if (row < m_num_qrows_per_bank) {
      auto rp = m_rpt[bank].find(row);
      if (rp != m_rpt[bank].end()) {
        int original = rp->second;
        m_rpt[bank].erase(row);
        m_rit_mapper->remove_entry(bank, original, row);
        m_rpt[bank][q_row] = original;
        m_rit_mapper->insert_entry(bank, original, q_row);
      }
    } else {
      // Migration: workload row → quarantine slot.
      m_rpt[bank][q_row] = row;
      m_rit_mapper->insert_entry(bank, row, q_row);
    }

    issue_migration(req.addr_vec, row, q_row);
    s_num_migrations++;

    m_rqa_head = (m_rqa_head + 1) % m_num_qrows_per_bank;
  }

 private:
  void issue_migration(const AddrVec_t& template_addr, int src_row, int dst_row) {
    if (m_is_debug) {
      std::cout << "AQUA: migration src=" << src_row << " dst=" << dst_row << std::endl;
    }
    issue_row_copy(template_addr, src_row, /*is_write=*/false);
    issue_row_copy(template_addr, dst_row, /*is_write=*/true);
  }

  void issue_row_copy(const AddrVec_t& template_addr, int row, bool is_write) {
    AddrVec_t addr = template_addr;
    addr[m_row_level] = row;
    int final_cmd = is_write ? m_wr_cmd_id : m_rd_cmd_id;
    for (int cl = 0; cl < m_num_cls; cl++) {
      addr[m_col_level] = cl * m_prefetch;
      Request req(addr, -1);
      req.command = final_cmd;
      req.final_command = final_cmd;
      m_ctrl->priority_send(req);
    }
  }
};

}  // namespace Ramulator
