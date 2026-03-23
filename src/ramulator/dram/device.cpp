#include "ramulator/dram/device.h"

namespace Ramulator {

void DRAMDevice::init(std::unique_ptr<DRAMSpec> spec) {
  m_spec_owner = std::move(spec);
  m_spec = m_spec_owner.get();
  m_bank_level = m_spec->get_level_id("Bank");
  m_root = std::make_unique<DRAMNode>(m_spec, nullptr, 0, 0);
  m_root->for_each_at_level(m_bank_level, [&](DRAMNode* bank) { m_bank_nodes.push_back(bank); });
}

void DRAMDevice::issue_command(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  m_root->update_timing(command, addr_vec, clk);
  apply_action(command, addr_vec, clk);
}

bool DRAMDevice::check_timing(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  return m_root->check_timing(command, addr_vec, clk);
}

int DRAMDevice::get_preq_command(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  auto preq_fn = m_spec->funcs.preqs[command];
  if (!preq_fn) {
    return command;
  }
  for (int bid : get_target_banks(command, addr_vec)) {
    int preq = preq_fn(m_bank_nodes[bid], command, addr_vec, clk);
    if (preq != command) {
      return preq;
    }
  }
  return command;
}

bool DRAMDevice::check_rowbuffer_hit(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  auto rowhit_fn = m_spec->funcs.rowhits[command];
  if (!rowhit_fn) {
    return false;
  }
  int bid = flat_bank_id(addr_vec);
  return rowhit_fn(m_bank_nodes[bid], command, addr_vec, clk);
}

bool DRAMDevice::check_node_open(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  auto rowopen_fn = m_spec->funcs.rowopens[command];
  if (!rowopen_fn) {
    return false;
  }
  int bid = flat_bank_id(addr_vec);
  return rowopen_fn(m_bank_nodes[bid], command, addr_vec, clk);
}

int DRAMDevice::flat_bank_id(const AddrVec_t& addr_vec) const {
  int id = 0;
  for (int lvl = 1; lvl <= m_bank_level; lvl++) {
    id = id * m_spec->organization.level_sizes[lvl] + addr_vec[lvl];
  }
  return id;
}

bool DRAMDevice::bank_matches(DRAMNode* bank, const AddrVec_t& addr_vec) {
  for (auto* n = bank; n != nullptr; n = n->m_parent_node) {
    if (addr_vec[n->m_level] != -1 && addr_vec[n->m_level] != n->m_node_id) {
      return false;
    }
  }
  return true;
}

std::vector<int> DRAMDevice::get_target_banks(int command, const AddrVec_t& addr_vec) const {
  switch (m_spec->bank_targets[command]) {
    case BankTarget::Single:
      return {flat_bank_id(addr_vec)};
    case BankTarget::All: {
      std::vector<int> ids;
      for (int i = 0; i < static_cast<int>(m_bank_nodes.size()); i++) {
        if (bank_matches(m_bank_nodes[i], addr_vec)) {
          ids.push_back(i);
        }
      }
      return ids;
    }
    case BankTarget::SameBank: {
      int target_bank_id = addr_vec[m_bank_level];
      std::vector<int> ids;
      for (int i = 0; i < static_cast<int>(m_bank_nodes.size()); i++) {
        if (m_bank_nodes[i]->m_node_id == target_bank_id && bank_matches(m_bank_nodes[i], addr_vec)) {
          ids.push_back(i);
        }
      }
      return ids;
    }
  }
  return {};
}

void DRAMDevice::apply_action(int command, const AddrVec_t& addr_vec, Clk_t clk) {
  auto action_fn = m_spec->funcs.actions[command];
  if (!action_fn) {
    return;
  }
  for (int bid : get_target_banks(command, addr_vec)) {
    action_fn(m_bank_nodes[bid], command, addr_vec, clk);
  }
}

}  // namespace Ramulator
