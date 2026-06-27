// RFMManager — generic threshold-driven Refresh-Management dispatcher.
//
// Tracks ACT counts per bank. When any bank's counter crosses
// `rfm_thresh`, issues an RFM command and resets the relevant counters.
// Dispatch mode is configurable:
//   - "ab" (default): rank-scoped RFMab, resets ALL bank counters.
//   - "pb":           per-bank RFMpb to the bank that crossed, resets
//                     ONLY that bank's counter.
//
// Requires a DRAM standard with the corresponding command(s) — RFMab for
// "ab" mode, RFMpb for "pb" mode.
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class RFMManager : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, RFMManager, "RFMManager")

 private:
  ControllerBase* m_ctrl = nullptr;

  int m_rfm_thresh = -1;
  std::string m_rfm_mode = "ab";
  bool m_debug = false;

  int m_rfm_cmd_id = -1;  // resolved at setup based on rfm_mode
  int m_rank_level = -1;
  int m_bank_level = -1;
  int m_row_level = -1;
  int m_bankgroup_level = -1;

  // Per-bank ACT counters indexed by flat bank id.
  std::vector<int> m_bank_ctrs;

  // Stats
  size_t s_rfm_counter = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_rfm_thresh, int, "rfm_thresh").default_val(80);
    RAMULATOR_PARSE_PARAM(m_rfm_mode, std::string, "rfm_mode").default_val("ab");
    RAMULATOR_PARSE_PARAM(m_debug, bool, "debug").default_val(false);

    if (m_rfm_mode != "ab" && m_rfm_mode != "pb") {
      throw std::runtime_error(
          "RFMManager: rfm_mode must be either 'ab' (all-bank) or 'pb' (per-bank); got '"
          + m_rfm_mode + "'");
    }
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    const std::string cmd_name = (m_rfm_mode == "ab") ? "RFMab" : "RFMpb";
    if (!spec->has_command(cmd_name)) {
      throw std::runtime_error(
          "RFMManager: rfm_mode='" + m_rfm_mode + "' requires DRAM standard with "
          + cmd_name + " command (e.g., DDR5_RFM or HBM3)");
    }

    m_rfm_cmd_id = spec->get_command_id(cmd_name);
    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");
    if (spec->has_level("BankGroup")) {
      m_bankgroup_level = spec->get_level_id("BankGroup");
    }

    int num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_bank_ctrs.assign(num_banks, 0);

    m_stats.add("rfm_counter", s_rfm_counter);
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;
    if (!spec->command_meta[req.command].is_opening) return;
    if (spec->bank_targets[req.command] != BankTarget::Single) return;

    int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    m_bank_ctrs[bank_id]++;

    if (m_debug) {
      std::cout << "Rank     : " << req.addr_vec[m_rank_level] << std::endl;
      std::cout << "Bank     : " << req.addr_vec[m_bank_level] << std::endl;
      if (m_bankgroup_level >= 0) {
        std::cout << "BankGroup: " << req.addr_vec[m_bankgroup_level] << std::endl;
      }
      std::cout << "Flat Bank: " << bank_id << std::endl;
    }

    if (m_bank_ctrs[bank_id] < m_rfm_thresh) {
      return;
    }

    AddrVec_t addr = req.addr_vec;
    if (m_rfm_mode == "ab") {
      // Rank-scoped
      if (m_bankgroup_level >= 0) addr[m_bankgroup_level] = -1;
      addr[m_bank_level] = -1;
    }
    // For pb mode, addr already targets the specific bank that crossed.

    Request rfm_req(addr, Request::Cmd, m_rfm_cmd_id);
    if (!m_ctrl->priority_send(rfm_req)) {
      throw std::runtime_error(
          "RFMManager: priority_send failed for " +
          std::string(m_rfm_mode == "ab" ? "RFMab" : "RFMpb") +
          " (priority buffer full)");
    }
    s_rfm_counter++;

    if (m_rfm_mode == "ab") {
      for (auto& c : m_bank_ctrs) c = 0;
    } else {
      m_bank_ctrs[bank_id] = 0;
    }
  }
};

}  // namespace Ramulator
