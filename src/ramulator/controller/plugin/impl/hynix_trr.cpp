// HynixTRR — models Hynix-style probabilistic in-DRAM TRR.
//
// The controller emits RFM commands (issued by RFMManager); HynixTRR
// models what the chip does during the RFM cycle.
//
// Per-bank sampled-row register: on each ACT, with probability
// `sample_probability` replace the bank's sampled row with the activated
// row. On each RFM:
//   - RFMab (rank-scoped): for every bank in the rank fire a VRR on the
//     sampled row (if any) and clear the register.
//   - RFMpb (per-bank): same, but only for the targeted bank.
//
// Requires a DRAM spec with VRR and at least one of RFMab/RFMpb.
#include <random>
#include <stdexcept>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class HynixTRR : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, HynixTRR, "HynixTRR")

 private:
  ControllerBase* m_ctrl = nullptr;

  float m_sample_probability = 0.0f;
  int m_seed = 0;
  std::mt19937 m_generator;
  std::uniform_real_distribution<float> m_distribution;

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

  // Per-bank sampled row register; -1 = none. Indexed by flat bank id.
  std::vector<int> m_sampled_row;

  // Stats
  size_t s_rfm_observed = 0;
  size_t s_vrrs_fired = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_sample_probability, float, "sample_probability").required();
    if (m_sample_probability <= 0.0f || m_sample_probability > 1.0f) {
      throw std::runtime_error("HynixTRR: sample_probability must be in (0, 1]");
    }
    RAMULATOR_PARSE_PARAM(m_seed, int, "seed").default_val(123);
    m_generator = std::mt19937(m_seed);
    m_distribution = std::uniform_real_distribution<float>(0.0, 1.0);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "HynixTRR requires a DRAM standard with VRR command (e.g. DDR5_RFM_VRR)");
    }
    if (!spec->has_command("RFMab") && !spec->has_command("RFMpb")) {
      throw std::runtime_error(
          "HynixTRR requires a DRAM standard with at least one of RFMab/RFMpb");
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

    m_sampled_row.assign(total_banks, -1);

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
    if (m_distribution(m_generator) >= m_sample_probability) return;
    int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    m_sampled_row[bank_id] = req.addr_vec[m_row_level];
  }

  void process_rfm_for_bank(const Request& req, int bank_id) {
    int row = m_sampled_row[bank_id];
    if (row < 0) return;

    int rank_id = bank_id / m_num_banks_per_rank;
    int local_bank = bank_id - rank_id * m_num_banks_per_rank;

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
    m_sampled_row[bank_id] = -1;
  }
};

}  // namespace Ramulator
