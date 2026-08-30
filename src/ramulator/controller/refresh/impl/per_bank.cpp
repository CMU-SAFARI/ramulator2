#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

// Per-bank refresh — issues REFpb to one bank at a time in round-robin order
// every nREFIpb cycles.
class PerBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, PerBankRefresh, "PerBank")

 private:
  ControllerBase* m_ctrl;
  Clk_t m_next_refresh_cycle = -1;
  int m_cmd_refpb = -1;
  int m_bank_level = -1;
  int m_nrefipb = -1;  // Cached nREFIpb timing value (cycles)
  int m_nrfc_pb = -1;
  bool m_requires_set_pause = false;
  DRAMNode* m_pending_boundary_bank = nullptr;
  Clk_t m_pending_boundary_enqueue_clk = -1;
  Clk_t m_next_set_allowed_clk = 0;

  std::vector<DRAMNode*> m_bank_nodes;
  size_t m_next_bank_idx = 0;

  AddrVec_t build_addr_vec(DRAMNode* node);
  void init() override;
  void tick() override;
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
  m_bank_level = info.get_level_id("Bank");
  m_nrefipb = info.get_timing_value("nREFIpb");
  m_requires_set_pause = info.standard_name == "GDDR6" || info.standard_name == "GDDR7";
  if (m_requires_set_pause) {
    m_nrfc_pb = info.get_timing_value("nRFCpb");
  }

  m_next_refresh_cycle = m_nrefipb;

  // Collect all bank-level nodes
  m_ctrl->m_device.m_root->for_each_at_level(m_bank_level, [&](DRAMNode* node) { m_bank_nodes.push_back(node); });
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

  // Refresh one bank in round-robin order.
  auto* bank_node = m_bank_nodes[m_next_bank_idx];
  AddrVec_t addr_vec = build_addr_vec(bank_node);
  Request req(addr_vec, Request::Cmd, m_cmd_refpb);

  bool is_success = m_ctrl->priority_send(req);
  if (!is_success) {
    throw std::runtime_error("Failed to send per-bank refresh!");
  }

  if (m_requires_set_pause && m_next_bank_idx + 1 == m_bank_nodes.size()) {
    m_pending_boundary_bank = bank_node;
    m_pending_boundary_enqueue_clk = m_ctrl->m_clk;
  }

  m_next_bank_idx = (m_next_bank_idx + 1) % m_bank_nodes.size();
  m_next_refresh_cycle += m_nrefipb;
}

}  // namespace Ramulator
