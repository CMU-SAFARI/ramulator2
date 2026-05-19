#include <cstdint>
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

// Randomized Row Swap.
//
// Hot-row detection uses the Graphene-style counting table with a
// spillover counter. When a physical row's ACT count crosses the swap
// threshold, the plugin issues a row swap against a random cold row via
// the RIT addr mapper; subsequent workload accesses to the original row
// are transparently redirected to the swap target by the mapper.
class RRS : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, RRS, "RRS")

 private:
  ControllerBase* m_ctrl = nullptr;
  RITAddrMapper* m_rit_mapper = nullptr;

  // Params
  int m_num_hrt_entries = -1;
  int m_num_rit_entries = -1;
  int m_rss_threshold = -1;
  int m_reset_period_ns = -1;
  bool m_is_debug = false;

  int m_reset_period_clk = -1;

  int m_rd_cmd_id = -1;
  int m_wr_cmd_id = -1;
  int m_row_level = -1;
  int m_col_level = -1;

  int m_num_banks = -1;
  int m_num_rows_per_bank = -1;
  int m_num_cls = -1;          // cache lines per row (= num_columns / prefetch)
  int m_prefetch = -1;         // burst size (column stride between cache lines)

  int m_clk = 0;

  // Hot-row tracker
  std::vector<std::unordered_map<int, int>> m_hrt;
  std::vector<int> m_spillover_counter;

  std::mt19937 m_generator;
  std::uniform_int_distribution<int> m_dist;

  // Stats
  size_t s_num_swaps = 0;
  size_t s_num_unswaps = 0;
  size_t s_num_reswaps = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_num_hrt_entries, int, "num_hrt_entries").required();
    RAMULATOR_PARSE_PARAM(m_num_rit_entries, int, "num_rit_entries").required();
    RAMULATOR_PARSE_PARAM(m_rss_threshold, int, "rss_threshold").required();
    RAMULATOR_PARSE_PARAM(m_reset_period_ns, int, "reset_period_ns").default_val(64000000);
    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    m_rit_mapper = dynamic_cast<RITAddrMapper*>(m_ctrl->m_addr_mapper);
    if (!m_rit_mapper) {
      throw std::runtime_error(
          "RRS requires the controller's addr_mapper to be RITAddrMapper");
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

    m_hrt.resize(m_num_banks);
    m_spillover_counter.assign(m_num_banks, 0);

    m_rit_mapper->init_rit(m_num_banks, m_num_rit_entries);

    m_generator = std::mt19937(1337);
    m_dist = std::uniform_int_distribution<int>(0, m_num_rows_per_bank);

    m_stats.add("rss_num_swaps", s_num_swaps);
    m_stats.add("rss_num_unswaps", s_num_unswaps);
    m_stats.add("rss_num_reswaps", s_num_reswaps);
  }

  void pre_schedule() override {
    m_clk++;

    // Epoch reset: clear HRT and unlock RIT entries.
    if (m_clk % m_reset_period_clk == 0) {
      for (auto& t : m_hrt) t.clear();
      std::fill(m_spillover_counter.begin(), m_spillover_counter.end(), 0);
      m_rit_mapper->rit_unlock();
      if (m_is_debug) {
        std::cout << "----------------------------" << std::endl;
        std::cout << "RRS is resetting. " << m_clk << std::endl;
        for (int b = 0; b < m_num_banks; b++) {
          m_rit_mapper->dump_rit(b);
        }
      }
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
      std::cout << "RRS: ACT on row " << row << "         " << m_clk << std::endl;
      std::cout << "  └  " << "bank: " << bank << std::endl;
    }

    // Update HRT with spillover eviction
    auto it = m_hrt[bank].find(row);
    if (it == m_hrt[bank].end()) {
      if (m_is_debug) {
        std::cout << "  └  " << "row " << row << " not in HRT." << std::endl;
      }
      if ((int)m_hrt[bank].size() < m_num_hrt_entries) {
        if (m_is_debug) {
          std::cout << "  └  " << "HRT is not full, inserting with count 1." << std::endl;
        }
        m_hrt[bank][row] = 1;
      } else {
        if (m_is_debug) {
          std::cout << "  └  " << "HRT is full, searching for a row to evict." << std::endl;
        }
        bool found = false;
        int to_remove = -1, spillover_value = -1;
        for (auto& [r, c] : m_hrt[bank]) {
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
          m_hrt[bank].erase(to_remove);
          m_hrt[bank][row] = spillover_value + 1;
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
      std::cout << "Row " << row << " in HRT" << std::endl;
      std::cout << "  └  " << "threshold: " << m_rss_threshold << std::endl;
      std::cout << "  └  " << "count: " << m_hrt[bank][row] << std::endl;
    }

    // Threshold check — only fire on multiples of the threshold.
    int count = m_hrt[bank][row];
    if (count % m_rss_threshold != 0) return;

    if (m_is_debug) {
      std::cout << "Row " << row << " needs swapping!" << std::endl;
    }

    // ── Swap logic ──────────────────────
    int prev_swapped = m_rit_mapper->check_rit(bank, row);

    if (prev_swapped != -1) {
      if (m_rit_mapper->is_rit_locked(bank, row)) {
        // Locked within same epoch — need to reswap both
        if (m_is_debug) {
          std::cout << "Row " << row << " is already swapped with row " << prev_swapped
                    << " in the current epoch." << std::endl;
          std::cout << "We need to swap both rows." << std::endl;
        }
        if (m_rit_mapper->is_rit_full(bank)) {
          auto [u_src, u_dst] = m_rit_mapper->get_unswap_pair(bank, m_hrt[bank]);
          if (m_is_debug) {
            std::cout << "RIT is full." << std::endl;
            std::cout << "Unswapping row " << u_src << " with row " << u_dst << std::endl;
          }
          issue_swap(req.addr_vec, u_src, u_dst);
          m_rit_mapper->remove_entry(bank, u_src, u_dst);
          s_num_unswaps++;
        }
        int dst0 = random_cold_row(bank, row);
        int dst1 = random_cold_row(bank, row);
        if (m_is_debug) {
          std::cout << "Swapping row " << row << " with row " << dst0 << std::endl;
          std::cout << "Swapping row " << prev_swapped << " with row " << dst1 << std::endl;
        }
        m_rit_mapper->remove_entry(bank, row, prev_swapped);
        issue_swap(req.addr_vec, row, dst0);
        issue_swap(req.addr_vec, prev_swapped, dst1);
        m_rit_mapper->insert_entry(bank, row, dst1);
        m_rit_mapper->insert_entry(bank, prev_swapped, dst0);
        s_num_swaps += 2;
        s_num_reswaps++;
      } else {
        // Unlocked from previous epoch — unswap first, then fresh swap.
        int dst = random_cold_row(bank, row);
        if (m_is_debug) {
          std::cout << "Row " << row << " is already swapped with row " << prev_swapped
                    << " in the previous epochs." << std::endl;
          std::cout << "We need to unswap and reswap the row." << std::endl;
          std::cout << "Unswapping row " << row << " with row " << prev_swapped << std::endl;
          std::cout << "Swapping row " << row << " with row " << dst << std::endl;
        }
        issue_swap(req.addr_vec, row, prev_swapped);
        m_rit_mapper->remove_entry(bank, row, prev_swapped);
        s_num_unswaps++;
        issue_swap(req.addr_vec, row, dst);
        m_rit_mapper->insert_entry(bank, row, dst);
        s_num_swaps++;
      }
    } else {
      // Fresh swap.
      if (m_rit_mapper->is_rit_full(bank)) {
        auto [u_src, u_dst] = m_rit_mapper->get_unswap_pair(bank, m_hrt[bank]);
        if (m_is_debug) {
          std::cout << "RIT is full." << std::endl;
          std::cout << "Unswapping row " << u_src << " with row " << u_dst << std::endl;
        }
        issue_swap(req.addr_vec, u_src, u_dst);
        m_rit_mapper->remove_entry(bank, u_src, u_dst);
        s_num_unswaps++;
      }
      int dst = random_cold_row(bank, row);
      if (m_is_debug) {
        std::cout << "Swapping row " << row << " with row " << dst << std::endl;
      }
      issue_swap(req.addr_vec, row, dst);
      m_rit_mapper->insert_entry(bank, row, dst);
      s_num_swaps++;
    }

    if (m_is_debug) {
      m_rit_mapper->dump_rit(bank);
    }
  }

 private:
  // Pick a random row that isn't: (1) the source itself, (2) hot in HRT,
  // (3) already in the RIT. Loops indefinitely until a valid row is
  // found — matches upstream Ramulator 2 behavior.
  int random_cold_row(int bank, int src) {
    int dst_row = -1;
    while (dst_row == -1) {
      int rand_row = m_dist(m_generator);
      if (m_hrt[bank].find(rand_row) == m_hrt[bank].end()
          && m_rit_mapper->check_rit(bank, rand_row) == -1
          && rand_row != src) {
        dst_row = rand_row;
      }
    }
    return dst_row;
  }

  // Model a row swap as a literal column-by-column copy in four phases:
  // read src, read dst, write dst, write src. Each phase issues
  // m_num_cls priority commands (one per cache line)
  void issue_swap(const AddrVec_t& template_addr, int src_row, int dst_row) {
    if (m_is_debug) {
      std::cout << "RRS: swap src=" << src_row << " dst=" << dst_row << std::endl;
    }
    issue_row_copy(template_addr, src_row, /*is_write=*/false);
    issue_row_copy(template_addr, dst_row, /*is_write=*/false);
    issue_row_copy(template_addr, dst_row, /*is_write=*/true);
    issue_row_copy(template_addr, src_row, /*is_write=*/true);
  }

  // Stream a whole row through the priority buffer, one priority send
  // per cache line
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
