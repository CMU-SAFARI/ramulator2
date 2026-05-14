#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/dram/node.h"

namespace Ramulator {
namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 9> all_bank_refresh_scopes = {{
    {"DDR3", "Rank"},
    {"DDR4", "Rank"},
    {"DDR5", "Rank"},
    {"LPDDR5", "Rank"},
    {"GDDR6", "Channel"},
    {"HBM1", "Channel"},
    {"HBM2", "PseudoChannel"},
    {"HBM3", "PseudoChannel"},
    {"HBM4", "PseudoChannel"},
}};

std::string all_bank_refresh_scope_for(const DRAMSpec& spec) {
  for (const auto& [standard, scope] : all_bank_refresh_scopes) {
    if (spec.standard_name == standard) {
      return std::string(scope);
    }
  }
  throw std::runtime_error("AllBank refresh: no default scope for DRAM standard '" + spec.standard_name + "'");
}

}  // namespace

// All-bank refresh — issues one REFab per standard-defined scope node every nREFI cycles.
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

  m_scope = all_bank_refresh_scope_for(info);
  m_cmd_refab = info.get_command_id("REFab");
  m_ref_level = info.get_level_id(m_scope);
  m_nrefi = info.get_timing_value("nREFI");

  m_next_refresh_cycle = m_nrefi;

  // Collect all nodes at the standard-defined scope — one refresh request per node
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
