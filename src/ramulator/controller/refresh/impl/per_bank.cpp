#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

// Per-bank refresh — issues REFpb to one target set (bank or bank pair) at a time
// in round-robin order every nREFIpb cycles.
class PerBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, PerBankRefresh, "PerBank")

 private:
  ControllerBase* m_ctrl;
  Clk_t m_next_refresh_cycle = -1;
  int m_cmd_refpb = -1;
  int m_cmd_rfmpb = -1;
  int m_nrefipb = -1;  // Cached nREFIpb timing value (cycles)
  int m_nrfc_pb = -1;
  bool m_requires_set_pause = false;
  DRAMNode* m_pending_boundary_bank = nullptr;
  Clk_t m_pending_boundary_enqueue_clk = -1;
  Clk_t m_next_set_allowed_clk = 0;

  std::vector<AddrVec_t> m_refresh_targets;  // one addr_vec per distinct REFpb target set
  size_t m_next_idx = 0;

  AddrVec_t build_addr_vec(DRAMNode* node);
  Clk_t set_pause_end() const;
  void init() override;
  void tick() override;
  bool blocks(int command, const AddrVec_t& addr_vec) const override;
};

AddrVec_t PerBankRefresh::build_addr_vec(DRAMNode* node) {
  AddrVec_t addr_vec(m_ctrl->m_device.m_spec->level_count, -1);
  for (auto* n = node; n != nullptr; n = n->m_parent_node) {
    addr_vec[n->m_level] = n->m_node_id;
  }
  return addr_vec;
}

void PerBankRefresh::init() {
  m_ctrl = cast_parent<ControllerBase>();
  const auto& info = *m_ctrl->m_device.m_spec;
  if (info.standard_name == "HBM1" || info.has_level("PseudoChannel")) {
    throw std::runtime_error(
        "PerBank refresh does not support HBM or PseudoChannel standards; use HBMPerBankRefresh instead");
  }
  m_cmd_refpb = info.get_command_id("REFpb");
  if (info.has_command("RFMpb")) {
    m_cmd_rfmpb = info.get_command_id("RFMpb");
  }
  m_nrefipb = info.get_timing_value("nREFIpb");
  m_requires_set_pause = info.standard_name == "GDDR6" || info.standard_name == "GDDR7";
  if (m_requires_set_pause) {
    m_nrfc_pb = info.get_timing_value("nRFCpb");
  }

  m_next_refresh_cycle = m_nrefipb;

  // Walk banks in order, skipping banks already covered by an earlier target set
  const auto& banks = m_ctrl->m_device.m_bank_nodes;
  std::vector<bool> covered(banks.size(), false);
  for (size_t i = 0; i < banks.size(); i++) {
    if (covered[i]) {
      continue;
    }
    AddrVec_t addr_vec = build_addr_vec(banks[i]);
    for (int bank_id : m_ctrl->m_device.get_target_banks(m_cmd_refpb, addr_vec)) {
      covered[bank_id] = true;
    }
    m_refresh_targets.push_back(addr_vec);
  }
}

void PerBankRefresh::tick() {
  if (m_pending_boundary_bank != nullptr) {
    const auto& history = m_pending_boundary_bank->m_cmd_history[m_cmd_refpb];
    if (history.empty()) {
      throw std::runtime_error("PerBank refresh requires Bank-level REFpb timing history");
    }
    Clk_t issue_clk = history.front();
    if (issue_clk >= m_pending_boundary_enqueue_clk) {
      m_pending_boundary_bank = nullptr;
      m_pending_boundary_enqueue_clk = -1;
      m_next_set_allowed_clk = issue_clk + m_nrfc_pb;
    }
  }

  if (m_ctrl->m_clk < m_next_refresh_cycle || m_pending_boundary_bank != nullptr ||
      m_ctrl->m_clk < m_next_set_allowed_clk) {
    return;
  }

  // Refresh one target set in round-robin order.
  const AddrVec_t& addr_vec = m_refresh_targets[m_next_idx];
  Request req(addr_vec, Request::Cmd, m_cmd_refpb);

  bool is_success = m_ctrl->priority_send(req);
  if (!is_success) {
    throw std::runtime_error("Failed to send per-bank refresh!");
  }

  if (m_requires_set_pause && m_next_idx + 1 == m_refresh_targets.size()) {
    m_pending_boundary_bank = m_ctrl->m_device.m_bank_nodes[m_ctrl->m_device.get_flat_bank_id(addr_vec)];
    m_pending_boundary_enqueue_clk = m_ctrl->m_clk;
  }

  m_next_idx = (m_next_idx + 1) % m_refresh_targets.size();
  m_next_refresh_cycle += m_nrefipb;
}

// End of the tRFCpb pause after the REFpb that completes a set, including a boundary REFpb
// that has issued but that tick() has not observed yet.
Clk_t PerBankRefresh::set_pause_end() const {
  if (m_pending_boundary_bank != nullptr) {
    const auto& history = m_pending_boundary_bank->m_cmd_history[m_cmd_refpb];
    if (!history.empty() && history.front() >= m_pending_boundary_enqueue_clk) {
      return history.front() + m_nrfc_pb;
    }
  }
  return m_next_set_allowed_clk;
}

// JESD239D §6.13 and Fig. 109: neither REFpb nor RFMpb may issue in the tRFCpb pause between two
// REFpb sets (JESD250C Table 47 Note 2 for REFpb). Inside a set, the timing graph's tRREFD applies.
bool PerBankRefresh::blocks(int command, const AddrVec_t& addr_vec) const {
  if (!m_requires_set_pause || (command != m_cmd_refpb && command != m_cmd_rfmpb)) {
    return false;
  }
  return m_ctrl->m_clk < set_pause_end();
}

}  // namespace Ramulator
