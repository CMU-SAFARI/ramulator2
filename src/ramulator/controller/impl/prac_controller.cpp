#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class PRACController : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, PRACController, ControllerBase, "PRAC")

 public:
  void init() override {
    init_base();

    RAMULATOR_PARSE_PARAM(m_abo_threshold, int, "abo_threshold").default_val(512);
    RAMULATOR_PARSE_PARAM(m_abo_act_ns, int, "abo_act_ns").default_val(180);
    RAMULATOR_PARSE_PARAM(m_abo_recovery_refs, int, "abo_recovery_refs").default_val(4);
    RAMULATOR_PARSE_PARAM(m_abo_delay_acts, int, "abo_delay_acts").default_val(4);
    RAMULATOR_PARSE_PARAM(m_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    setup_base(frontend, memory_system);

    auto* spec = m_device.m_spec;

    if (!spec->has_command("RFMab")) {
      throw std::runtime_error(
          "PRACController requires a DRAM standard with RFMab command "
          "(e.g., DDR5_RFM or HBM3)!");
    }

    m_cmd_act = spec->get_command_id("ACT");
    m_cmd_rfmab = spec->get_command_id("RFMab");
    m_cmd_preab = spec->get_command_id("PREab");
    // bankgroup-scoped per-bank-group RFM
    if (spec->has_command("RFMsb")) {
      m_cmd_rfmsb = spec->get_command_id("RFMsb");
    }
    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");
    if (spec->has_level("BankGroup")) {
      m_bankgroup_level = spec->get_level_id("BankGroup");
      m_num_banks_per_bankgroup = spec->get_level_size("Bank");
    }

    m_abo_act_cycles = m_abo_act_ns / (spec->get_timing_value("tCK_ps") / 1000.0f);

    int num_banks = m_device.m_bank_nodes.size();
    m_num_ranks = spec->get_level_size("Rank");
    m_num_banks_per_rank = num_banks / m_num_ranks;
    m_bank_counters.resize(num_banks);
    m_critical_rows.resize(num_banks);

    int nRP = spec->get_timing_value("nRP");
    int nRAS = spec->get_timing_value("nRAS");
    int nRTP = spec->get_timing_value("nRTP");
    int nCWL = spec->get_timing_value("nCWL");
    int nBL = spec->get_timing_value("nBL");
    int nWR = spec->get_timing_value("nWR");
    int write_to_pre_timing = nCWL + nBL + nWR;
    int write_cycles = nRAS + write_to_pre_timing + nRP;
    m_cmd_to_min_cycles[m_cmd_act] = write_cycles;
    m_cmd_to_min_cycles[spec->get_command_id("RD")] = nRTP + nRP;
    m_cmd_to_min_cycles[spec->get_command_id("WR")] = write_to_pre_timing + nRP;
    m_cmd_to_min_cycles[m_cmd_rfmab] = spec->get_timing_value("nRFM");
    if (spec->has_command("RFMpb")) {
      m_cmd_to_min_cycles[spec->get_command_id("RFMpb")] = spec->get_timing_value("nRFMpb");
    }
    if (spec->has_command("REFab")) {
      m_cmd_to_min_cycles[spec->get_command_id("REFab")] = spec->get_timing_value("nRFC");
    }

    m_nRP = nRP;
    m_prac_buffer.max_size = std::numeric_limits<int>::max();

    m_stats.add("prac_num_recovery", s_num_recovery);
  }

  void tick() override;

 private:
  // ABO state machine
  enum class ABOState { NORMAL, PRE_RECOVERY, RECOVERY, DELAY };
  ABOState m_abo_state = ABOState::NORMAL;

  // Config
  int m_abo_threshold = -1;
  int m_abo_act_ns = -1;
  int m_abo_recovery_refs = -1;
  int m_abo_delay_acts = -1;
  int m_abo_act_cycles = -1;
  bool m_debug = false;

  // Runtime state
  Clk_t m_abo_recovery_start = std::numeric_limits<Clk_t>::max();
  int m_abo_recov_rem_refs = 0;
  int m_abo_delay_rem_acts = 0;
  bool m_abo_needed = false;

  // Spec lookups
  int m_cmd_act = -1;
  int m_cmd_rfmab = -1;
  int m_cmd_rfmsb = -1;  // -1 if spec doesn't have per-bank RFM (e.g. plain DDR5_RFM)
  int m_cmd_preab = -1;
  int m_rank_level = -1;
  int m_bankgroup_level = -1;
  int m_bank_level = -1;
  int m_row_level = -1;
  int m_num_ranks = -1;
  int m_num_banks_per_rank = -1;
  int m_num_banks_per_bankgroup = -1;

  // Per-bank row activation counters
  std::vector<std::unordered_map<int, int>> m_bank_counters;
  std::vector<std::unordered_map<int, int>> m_critical_rows;

  ReqBuffer m_prac_buffer;
  std::unordered_map<int, int> m_cmd_to_min_cycles;
  bool m_recovery_setup = false;
  int m_nRP = -1;

  // Stats
  size_t s_num_recovery = 0;

  // ── PRAC logic ──────────────────────────────────────────────────────

  // Process a request as if it targets `bank_id`. Mirrors upstream's
  // PerBankCounters::on_request — dispatches by command and updates
  // counter / critical_row state for the bank.
  void on_request_for_bank(int bank_id, const Request& req) {
    if (req.command == m_cmd_act) {
      int row_addr = req.addr_vec[m_row_level];
      auto& count = m_bank_counters[bank_id][row_addr];
      count++;
      if (m_debug) {
        std::printf("[PRAC] [%d] [ACT] Row: %d Act: %d\n",
                    bank_id, row_addr, count);
      }
      if (count >= m_abo_threshold) {
        m_critical_rows[bank_id][row_addr] = count;
        m_abo_needed = true;
      }
    } else if (req.command == m_cmd_rfmab ||
               (m_cmd_rfmsb >= 0 && req.command == m_cmd_rfmsb)) {
      auto& counters = m_bank_counters[bank_id];
      if (counters.empty()) {
        if (m_debug) {
          std::printf("[PRAC] [%d] [RFM] No critical row.\n", bank_id);
        }
        return;
      }
      auto max_it = counters.begin();
      for (auto it = counters.begin(); it != counters.end(); ++it) {
        if (it->second > max_it->second) max_it = it;
      }
      if (m_debug) {
        std::printf("[PRAC] [%d] [RFM] Row: %d Act: %d\n",
                    bank_id, max_it->first, max_it->second);
      }
      int max_row = max_it->first;
      max_it->second = 0;
      m_critical_rows[bank_id].erase(max_row);
    }
  }

  // Wildcard-bank dispatcher (matches upstream's update() shape):
  //   bank=-1 && bankgroup=-1: all bankgroups, all banks   (e.g. RFMab)
  //   bank=*  && bankgroup=-1: same bank id in every bankgroup
  //   bank=-1 && bankgroup=*:  all banks of one bankgroup  (e.g. RFMsb)
  //   bank=*  && bankgroup=*:  single bank                 (e.g. ACT)
  void update_counters(const Request& req) {
    bool has_bank_wild = req.addr_vec[m_bank_level] == -1;
    bool has_bg_wild =
        m_bankgroup_level >= 0 && req.addr_vec[m_bankgroup_level] == -1;

    if (has_bg_wild && has_bank_wild) {
      int offset = req.addr_vec[m_rank_level] * m_num_banks_per_rank;
      for (int i = 0; i < m_num_banks_per_rank; i++) {
        on_request_for_bank(offset + i, req);
      }
    } else if (has_bg_wild) {
      int rank_offset = req.addr_vec[m_rank_level] * m_num_banks_per_rank;
      int bank_offset = req.addr_vec[m_bank_level];
      int num_bgs = m_num_banks_per_rank / m_num_banks_per_bankgroup;
      for (int i = 0; i < num_bgs; i++) {
        on_request_for_bank(
            rank_offset + i * m_num_banks_per_bankgroup + bank_offset, req);
      }
    } else if (has_bank_wild) {
      int rank_offset = req.addr_vec[m_rank_level] * m_num_banks_per_rank;
      int bg_offset =
          req.addr_vec[m_bankgroup_level] * m_num_banks_per_bankgroup;
      for (int i = 0; i < m_num_banks_per_bankgroup; i++) {
        on_request_for_bank(rank_offset + bg_offset + i, req);
      }
    } else {
      on_request_for_bank(m_device.get_flat_bank_id(req.addr_vec), req);
    }
  }

  static const char* state_name(ABOState s) {
    switch (s) {
      case ABOState::NORMAL: return "ABOState::NORMAL";
      case ABOState::PRE_RECOVERY: return "ABOState::PRE_RECOVERY";
      case ABOState::RECOVERY: return "ABOState::RECOVERY";
      case ABOState::DELAY: return "ABOState::DELAY";
    }
    return "ABOState::?";
  }

  void update_state_machine(bool request_found, const Request& req) {
    ABOState cur_state = m_abo_state;
    switch (m_abo_state) {
      case ABOState::NORMAL:
        if (m_abo_needed) {
          if (m_debug) {
            std::printf("[PRAC] [%lu] <%s> Asserting ALERT_N.\n",
                        (unsigned long)m_clk, state_name(cur_state));
          }
          m_abo_state = ABOState::PRE_RECOVERY;
          m_abo_recovery_start = m_clk + m_abo_act_cycles;
          s_num_recovery++;
        }
        break;

      case ABOState::PRE_RECOVERY:
        if (m_debug && request_found && req.command == m_cmd_preab) {
          std::printf("[PRAC] [%lu] <%s> Received PREA.\n",
                      (unsigned long)m_clk, state_name(cur_state));
        }
        if (m_clk == m_abo_recovery_start) {
          m_abo_state = ABOState::RECOVERY;
          m_abo_recovery_start = std::numeric_limits<Clk_t>::max();
          m_abo_recov_rem_refs = m_abo_recovery_refs * m_num_ranks;
        }
        break;

      case ABOState::RECOVERY:
        if (request_found &&
            (req.command == m_cmd_rfmab ||
             (m_cmd_rfmsb >= 0 && req.command == m_cmd_rfmsb))) {
          m_abo_recov_rem_refs--;
          if (m_abo_recov_rem_refs <= 0) {
            m_abo_state = ABOState::DELAY;
            m_abo_delay_rem_acts = m_abo_delay_acts;
          }
        }
        break;

      case ABOState::DELAY:
        if (request_found && req.command == m_cmd_act) {
          m_abo_delay_rem_acts--;
          if (m_abo_delay_rem_acts <= 0) {
            // per-bank check — m_critical_rows shadows the set of
            // rows currently at or above abo_threshold.
            m_abo_needed = false;
            for (auto& bank : m_critical_rows) {
              if (!bank.empty()) {
                m_abo_needed = true;
                break;
              }
            }
            m_abo_state = ABOState::NORMAL;
            m_recovery_setup = false;
          }
        }
        break;
    }
    if (m_debug && cur_state != m_abo_state) {
      std::printf("[PRAC] [%lu] <%s> -> <%s>\n",
                  (unsigned long)m_clk, state_name(cur_state), state_name(m_abo_state));
    }
  }

  int min_cycles_with_preall(const Request& req) const {
    auto it = m_cmd_to_min_cycles.find(req.command);
    return (it == m_cmd_to_min_cycles.end()) ? 0 : it->second;
  }

  void inject_recovery_commands() {
    AddrVec_t addr(m_device.m_spec->level_count, 0);
    addr[0] = m_channel_id;
    for (int r = 0; r < m_num_ranks; r++) {
      addr[m_rank_level] = r;
      Request prea(addr, Request::Cmd, m_cmd_preab);
      m_prac_buffer.enqueue(prea);
    }
    for (int i = 0; i < m_abo_recovery_refs; i++) {
      for (int r = 0; r < m_num_ranks; r++) {
        addr[m_rank_level] = r;
        Request rfm(addr, Request::Cmd, m_cmd_rfmab);
        m_prac_buffer.enqueue(rfm);
      }
    }
  }
};

void PRACController::tick() {
  tick_prologue();

  m_refresh->tick();

  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  if (m_abo_recovery_start != std::numeric_limits<Clk_t>::max() &&
      !m_recovery_setup &&
      m_clk + (Clk_t)(m_nRP + 5) >= m_abo_recovery_start) {
    inject_recovery_commands();
    m_recovery_setup = true;
  }

  auto fits_filter = [&](const Request& req) -> bool {
    return m_clk + (Clk_t)min_cycles_with_preall(req) < m_abo_recovery_start;
  };
  auto prac_buffer_filter = [&](const Request& req) -> bool {
    if (req.command == m_cmd_rfmab && m_abo_state == ABOState::PRE_RECOVERY) {
      return false;
    }
    return true;
  };

  Candidate cand = pick_best_ready_from(m_active_buffer, fits_filter);

  bool prac_buffer_stalled = false;
  if (!cand.valid && m_prac_buffer.size() > 0) {
    cand = pick_best_ready_from(m_prac_buffer, prac_buffer_filter);
    if (!cand.valid) {
      prac_buffer_stalled = true;
    }
  }

  if (!cand.valid && !prac_buffer_stalled) {
    cand = pick_priority_if(fits_filter);
  }

  if (!cand.valid && !prac_buffer_stalled && m_priority_buffer.size() == 0) {
    cand = pick_rw_if(fits_filter);
  }

  bool request_found = cand.valid;
  Request issued_req;
  if (cand.valid) {
    m_rowpolicy->try_upgrade_command(*cand.it);

    if (!cand.it->is_stat_updated) {
      update_request_stats(cand.it);
    }

    m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);

    m_rowpolicy->on_issue(*cand.it);
    for (auto* p : m_plugins) {
      p->on_issue(*cand.it);
    }

    update_counters(*cand.it);
    issued_req = *cand.it;

    if (cand.it->command == cand.it->final_command) {
      retire_request(cand.it, *cand.buffer);
    } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
      promote_to_active(cand.it, *cand.buffer);
    }
  }

  update_state_machine(request_found, issued_req);

  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

}  // namespace Ramulator
