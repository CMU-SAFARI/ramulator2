// IdealTRR — perfect-tracking in-DRAM TRR.
//
// Unbounded per-bank counter table: counts every ACT exactly, never
// evicts. On each RFM (ab or pb), picks the top `num_rows_per_rfm`
// rows by count and fires VRR on each, resetting their counts to 0.
// Requires a DRAM spec with VRR and at least one of RFMab/RFMpb.
#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class IdealTRR : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, IdealTRR, "IdealTRR")

 private:
  ControllerBase* m_ctrl = nullptr;

  int m_num_rows_per_rfm = 1;

  int m_act_cmd_id = -1;
  int m_vrr_cmd_id = -1;
  int m_rfm_ab_cmd_id = -1;
  int m_rfm_pb_cmd_id = -1;

  int m_rank_level = -1;
  int m_bankgroup_level = -1;
  int m_bank_level = -1;
  int m_row_level = -1;

  int m_num_ranks = -1;
  int m_num_banks_per_rank = -1;
  int m_num_banks_per_bankgroup = -1;

  // Per-bank unbounded counter tables, indexed by flat bank id.
  // No eviction, no spillover, every ACT is counted exactly.
  std::vector<std::unordered_map<int, int>> m_counter_table;

  // Stats
  size_t s_rfm_observed = 0;
  size_t s_vrrs_fired = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_num_rows_per_rfm, int, "num_rows_per_rfm").default_val(1);
    if (m_num_rows_per_rfm < 1) {
      throw std::runtime_error("IdealTRR: num_rows_per_rfm must be >= 1");
    }
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "IdealTRR requires a DRAM standard with VRR command (e.g. DDR5_RFM_VRR)");
    }
    if (!spec->has_command("RFMab") && !spec->has_command("RFMpb")) {
      throw std::runtime_error(
          "IdealTRR requires a DRAM standard with at least one of RFMab/RFMpb");
    }

    m_act_cmd_id = spec->get_command_id("ACT");
    m_vrr_cmd_id = spec->get_command_id("VRR");
    if (spec->has_command("RFMab")) m_rfm_ab_cmd_id = spec->get_command_id("RFMab");
    if (spec->has_command("RFMpb")) m_rfm_pb_cmd_id = spec->get_command_id("RFMpb");

    m_rank_level = spec->get_level_id("Rank");
    m_bank_level = spec->get_level_id("Bank");
    m_row_level = spec->get_level_id("Row");
    if (spec->has_level("BankGroup")) {
      m_bankgroup_level = spec->get_level_id("BankGroup");
      m_num_banks_per_bankgroup = spec->get_level_size("Bank");
    }

    m_num_ranks = spec->get_level_size("Rank");
    int total_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_num_banks_per_rank = total_banks / m_num_ranks;

    m_counter_table.assign(total_banks, {});

    m_stats.add("rfm_observed", s_rfm_observed);
    m_stats.add("vrrs_fired", s_vrrs_fired);
  }

  void on_issue(const Request& req) override {
    if (req.command == m_rfm_ab_cmd_id) {
      s_rfm_observed++;
      int rank_id = req.addr_vec[m_rank_level];
      int rank_start = rank_id * m_num_banks_per_rank;
      int rank_end = rank_start + m_num_banks_per_rank;
      for (int bank_id = rank_start; bank_id < rank_end; bank_id++) {
        process_rfm_for_bank(req, bank_id);
      }
      return;
    }
    if (req.command == m_rfm_pb_cmd_id) {
      s_rfm_observed++;
      int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
      process_rfm_for_bank(req, bank_id);
      return;
    }
    if (req.command != m_act_cmd_id) {
      return;
    }
    process_act(req);
  }

 private:
  void process_act(const Request& req) {
    int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    int row_id = req.addr_vec[m_row_level];
    m_counter_table[bank_id][row_id]++;
  }

  void process_rfm_for_bank(const Request& req, int bank_id) {
    auto& table = m_counter_table[bank_id];
    if (table.empty()) return;

    int rank_id = bank_id / m_num_banks_per_rank;
    int local_bank = bank_id - rank_id * m_num_banks_per_rank;

    // Pick top-K by count.
    std::vector<std::pair<int, int>> entries(table.begin(), table.end());
    int k = std::min(m_num_rows_per_rfm, (int)entries.size());
    std::partial_sort(
        entries.begin(), entries.begin() + k, entries.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    for (int i = 0; i < k; i++) {
      int row = entries[i].first;
      AddrVec_t vrr_addr(req.addr_vec);
      vrr_addr[m_rank_level] = rank_id;
      if (m_bankgroup_level >= 0) {
        vrr_addr[m_bankgroup_level] = local_bank / m_num_banks_per_bankgroup;
        vrr_addr[m_bank_level] = local_bank % m_num_banks_per_bankgroup;
      } else {
        vrr_addr[m_bank_level] = local_bank;
      }
      vrr_addr[m_row_level] = row;

      Request vrr_req(vrr_addr, Request::Cmd, m_vrr_cmd_id);
      if (m_ctrl->priority_send(vrr_req)) {
        s_vrrs_fired++;
      }

      // Reset the chosen row's count.
      table[row] = 0;
    }
  }
};

}  // namespace Ramulator
