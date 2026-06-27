#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/dram/node.h"

namespace Ramulator {
namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 11> all_bank_refresh_scopes = {{
    {"DDR3", "Rank"},
    {"DDR4", "Rank"},
    {"DDR5", "Rank"},
    {"LPDDR5", "Rank"},
    {"LPDDR6", "Rank"},
    {"GDDR6", "Channel"},
    {"GDDR7", "Channel"},
    {"HBM1", "Channel"},
    {"HBM2", "PseudoChannel"},
    {"HBM3", "PseudoChannel"},
    {"HBM4", "PseudoChannel"},
}};


// All-bank refresh
//
// Default behavior:
//   scatter_interval <= 0:
//      identical to the "old" AllBankRefresh:
//      every nREFI cycles, issue one REFab to every refresh-scope node
//
// Scattered behavior:
//   scatter_interval > 0:
//      issue refreshes to different refresh-scope nodes at staggered offsets
//      Each node is still refreshed once per nREFI period

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

  // optional: 0 or not-specified -> "old" behaviour
  int m_scatter_interval = 0;
  bool m_scatter_enabled = false;

  bool m_debug = false;

  std::vector<DRAMNode*> m_ref_nodes;
  std::vector<Clk_t> m_next_scattered_refresh_cycles;

  AddrVec_t build_addr_vec(DRAMNode* node);
  void init() override;
  void tick() override;

  void tick_all_at_once();
  void tick_scattered();
  void send_refresh(DRAMNode* ref_node);
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
  RAMULATOR_PARSE_PARAM(m_scatter_interval, int, "scatter_interval").default_val(0);
  RAMULATOR_PARSE_PARAM(m_debug, bool, "debug").default_val(false);
  const auto& info = *m_ctrl->m_device.m_spec;

  m_scope = all_bank_refresh_scope_for(info);
  m_cmd_refab = info.get_command_id("REFab");
  m_ref_level = info.get_level_id(m_scope);
  m_nrefi = info.get_timing_value("nREFI");


  // Collect all nodes at the standard-defined scope — one refresh request per node
  m_ctrl->m_device.m_root->for_each_at_level(m_ref_level, [&](DRAMNode* node) { m_ref_nodes.push_back(node); });

  if (m_ref_nodes.empty()) {
    throw std::runtime_error("AllBank refresh: no nodes found at level '" + m_scope + "'");
  }

  m_scatter_enabled = m_scatter_interval > 0;
  if (!m_scatter_enabled) { // "old" refresh behaviour
    m_next_refresh_cycle = m_nrefi;
    if (m_debug) {
      std::cout << "[AllBank:init] scope=" << m_scope
                << " nREFI=" << m_nrefi
                << " ref_nodes=" << m_ref_nodes.size()
                << " scatter=disabled"
                << std::endl;
    }
    return;
  }

  // scattered refresh behaviour
  const Clk_t interval = static_cast<Clk_t>(m_scatter_interval);
  const Clk_t num_nodes = static_cast<Clk_t>(m_ref_nodes.size());
  if (interval <= 0) {
    throw std::runtime_error(
        "AllBank refresh: scatter_interval must be positive when scattering is enabled"
    );
  }
  if (interval * num_nodes > m_nrefi) {
    throw std::runtime_error("AllBank refresh: scatter_interval * number_of_refresh_nodes must be <= nREFI");
  }
  m_next_scattered_refresh_cycles.resize(m_ref_nodes.size());
  for (size_t i = 0; i < m_ref_nodes.size(); i++) {
    m_next_scattered_refresh_cycles[i] = static_cast<Clk_t>(i + 1) * interval;
  }
  if (m_debug) {
    std::cout << "[AllBank:init] scope=" << m_scope
              << " nREFI=" << m_nrefi
              << " ref_nodes=" << m_ref_nodes.size()
              << " scatter=enabled"
              << " scatter_interval=" << interval
              << std::endl;
  }

}

void AllBankRefresh::tick() {
  if (m_scatter_enabled) {
    tick_scattered();
  } else {
    tick_all_at_once();
  }
}

void AllBankRefresh::send_refresh(DRAMNode* ref_node) {
  AddrVec_t addr_vec = build_addr_vec(ref_node);
  Request req(addr_vec, Request::Cmd, m_cmd_refab);
  if (m_debug) {
    std::cout << "[AllBank] clk=" << m_ctrl->m_clk << " addr_vec=[";
    for (size_t j = 0; j < addr_vec.size(); j++) {
      std::cout << addr_vec[j];
      if (j + 1 < addr_vec.size()) {
        std::cout << ",";
      }
    }
    std::cout << "]" << std::endl;
  }

  bool is_success = m_ctrl->priority_send(req);
  if (!is_success) throw std::runtime_error("Failed to send all-bank refresh!");
}

void AllBankRefresh::tick_all_at_once() {
  if (m_ctrl->m_clk != m_next_refresh_cycle) {
    return;
  }
  m_next_refresh_cycle += m_nrefi;
  for (auto* ref_node : m_ref_nodes) {
    send_refresh(ref_node);
  }
}

void AllBankRefresh::tick_scattered() {
  for (size_t i = 0; i < m_ref_nodes.size(); i++) {
    if (m_ctrl->m_clk != m_next_scattered_refresh_cycles[i]) continue;
    m_next_scattered_refresh_cycles[i] += m_nrefi;
    send_refresh(m_ref_nodes[i]);
  }
}

}  // namespace Ramulator
