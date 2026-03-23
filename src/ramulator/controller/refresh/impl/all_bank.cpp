#include <string>
#include <vector>

#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

// All-bank refresh — issues one REFab per node at a configurable hierarchy
// scope every nREFI cycles.  Typical scope is "Rank" (DDR3/4/5); standards
// without a Rank (e.g. HBM) use scope "Channel".
class AllBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, AllBankRefresh, "AllBank")

 private:
  ControllerBase* m_ctrl;
  std::string m_scope;
  Clk_t m_next_refresh_cycle = -1;
  int m_cmd_refab = -1;
  int m_ref_level = -1;
  int m_nrefi = -1;  // Cached nREFI timing value (cycles)

  std::vector<DRAMNode*> m_ref_nodes;

  AddrVec_t build_addr_vec(DRAMNode* node);
  void init() override;
  void tick() override;
};

AddrVec_t AllBankRefresh::build_addr_vec(DRAMNode* node) {
  AddrVec_t addr_vec(m_ctrl->m_device.m_spec->level_count, -1);
  for (auto* n = node; n != nullptr; n = n->m_parent_node) {
    addr_vec[n->m_level] = n->m_node_id;
  }
  return addr_vec;
}

void AllBankRefresh::init() {
  m_ctrl = cast_parent<ControllerBase>();
  const auto& info = *m_ctrl->m_device.m_spec;

  RAMULATOR_PARSE_PARAM(m_scope, std::string, "scope").required();

  m_cmd_refab = info.get_command_id("REFab");
  m_ref_level = info.get_level_id(m_scope);
  m_nrefi = info.get_timing_value("nREFI");

  m_next_refresh_cycle = m_nrefi;

  // Collect all nodes at the configured level — one refresh request per node
  m_ctrl->m_device.m_root->for_each_at_level(m_ref_level, [&](DRAMNode* node) { m_ref_nodes.push_back(node); });

  if (m_ref_nodes.empty()) {
    throw std::runtime_error("AllBank refresh: no nodes found at level '" + m_scope + "'");
  }
}

void AllBankRefresh::tick() {
  if (m_ctrl->m_clk == m_next_refresh_cycle) {
    m_next_refresh_cycle += m_nrefi;
    for (auto* ref_node : m_ref_nodes) {
      AddrVec_t addr_vec = build_addr_vec(ref_node);
      Request req(addr_vec, Request::Cmd, m_cmd_refab);

      bool is_success = m_ctrl->priority_send(req);
      if (!is_success) {
        throw std::runtime_error("Failed to send refresh!");
      }
    }
  }
}

}  // namespace Ramulator
