#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

class HBMPerBankRefreshCore {
 private:
  struct BankRef {
    DRAMNode* node = nullptr;
    int scope = -1;
    int flat_bank = -1;
  };

  struct RefpbSetState {
    std::vector<BankRef> banks_by_flat;
    DRAMNode* pending_boundary_bank = nullptr;
    Clk_t pending_boundary_enqueue_clk = -1;
    Clk_t next_block_allowed_clk = 0;
  };

  ControllerBase* m_ctrl = nullptr;
  int m_cmd_refpb = -1;
  int m_level_scope = -1;
  int m_level_sid = -1;
  int m_level_bg = -1;
  int m_level_bank = -1;
  int m_scope_count = 0;
  int m_sid_count = 0;
  int m_bank_groups = 0;
  int m_banks_per_group = 0;
  int m_banks_per_sid = 0;
  int m_banks_per_scope = 0;
  int m_boundary_block_size = 0;
  int m_nrefipb = -1;
  int m_nrfc_pb = -1;
  int m_next_flat_bank = 0;
  Clk_t m_next_refresh_clk = -1;

  std::vector<RefpbSetState> m_sets;
  std::deque<BankRef> m_pending_refpbs;

  AddrVec_t build_addr_vec(DRAMNode* node) const;
  int flat_bank_in_scope(const AddrVec_t& addr_vec) const;
  void build_refpb_sets();
  void observe_boundary_refpbs();
  bool seed_pending_refpbs();
  bool service_pending_refpb();

 public:
  void init(ControllerBase* ctrl);
  void tick();
};

AddrVec_t HBMPerBankRefreshCore::build_addr_vec(DRAMNode* node) const {
  AddrVec_t addr_vec(m_ctrl->m_device.m_spec->level_count, -1);
  for (auto* n = node; n != nullptr; n = n->m_parent_node) {
    addr_vec[n->m_level] = n->m_node_id;
  }
  return addr_vec;
}

int HBMPerBankRefreshCore::flat_bank_in_scope(const AddrVec_t& addr_vec) const {
  int sid = m_level_sid >= 0 ? addr_vec[m_level_sid] : 0;
  int flat_bank_in_sid = addr_vec[m_level_bg] * m_banks_per_group + addr_vec[m_level_bank];
  return sid * m_banks_per_sid + flat_bank_in_sid;
}

void HBMPerBankRefreshCore::init(ControllerBase* ctrl) {
  m_ctrl = ctrl;
  const auto& info = *m_ctrl->m_device.m_spec;
  if (info.standard_name != "HBM1" && info.standard_name != "HBM2" &&
      info.standard_name != "HBM3" && info.standard_name != "HBM4") {
    throw std::runtime_error("HBMPerBankRefresh requires an HBM1, HBM2, HBM3, or HBM4 standard");
  }

  m_cmd_refpb = info.get_command_id("REFpb");
  m_level_scope = info.has_level("PseudoChannel") ? info.get_level_id("PseudoChannel")
                                                   : info.get_level_id("Channel");
  m_level_sid = info.has_level("Sid") ? info.get_level_id("Sid") : -1;
  m_level_bg = info.get_level_id("BankGroup");
  m_level_bank = info.get_level_id("Bank");
  m_scope_count = info.organization.level_sizes[m_level_scope];
  m_sid_count = m_level_sid >= 0 ? info.get_level_size("Sid") : 1;
  m_bank_groups = info.get_level_size("BankGroup");
  m_banks_per_group = info.get_level_size("Bank");
  m_banks_per_sid = m_bank_groups * m_banks_per_group;
  m_banks_per_scope = m_sid_count * m_banks_per_sid;
  // JESD235D uses one REFSB set per selected channel/pseudochannel, spanning
  // all of its SIDs and banks. JESD238 and JESD270-4 use a 16-bank REFpb set
  // per SID.
  m_boundary_block_size =
      (info.standard_name == "HBM3" || info.standard_name == "HBM4")
          ? m_banks_per_sid
          : m_banks_per_scope;
  m_nrefipb = info.get_timing_value("nREFIpb");
  m_nrfc_pb = info.get_timing_value("nRFCpb");
  m_next_refresh_clk = m_nrefipb;

  build_refpb_sets();
}

void HBMPerBankRefreshCore::build_refpb_sets() {
  m_sets.assign(m_scope_count, RefpbSetState{});
  for (auto& set : m_sets) {
    set.banks_by_flat.resize(m_banks_per_scope);
  }

  m_ctrl->m_device.m_root->for_each_at_level(m_level_bank, [&](DRAMNode* node) {
    AddrVec_t addr_vec = build_addr_vec(node);
    int scope = addr_vec[m_level_scope];
    int flat_bank = flat_bank_in_scope(addr_vec);
    auto& slot = m_sets[scope].banks_by_flat[flat_bank];
    if (slot.node != nullptr) {
      throw std::runtime_error("HBMPerBankRefresh found duplicate bank node in a REFpb set");
    }
    slot = BankRef{node, scope, flat_bank};
  });

  for (const auto& set : m_sets) {
    for (const auto& bank : set.banks_by_flat) {
      if (bank.node == nullptr) {
        throw std::runtime_error("HBMPerBankRefresh found an incomplete REFpb set");
      }
    }
  }
}

void HBMPerBankRefreshCore::observe_boundary_refpbs() {
  for (auto& set : m_sets) {
    if (set.pending_boundary_bank == nullptr) {
      continue;
    }

    const auto& history = set.pending_boundary_bank->m_cmd_history[m_cmd_refpb];
    if (history.empty()) {
      throw std::runtime_error("HBMPerBankRefresh requires Bank-level REFpb timing history");
    }

    Clk_t issue_clk = history.front();
    if (issue_clk < set.pending_boundary_enqueue_clk) {
      continue;
    }

    set.pending_boundary_bank = nullptr;
    set.pending_boundary_enqueue_clk = -1;
    set.next_block_allowed_clk = issue_clk + m_nrfc_pb;
  }
}

bool HBMPerBankRefreshCore::seed_pending_refpbs() {
  for (const auto& set : m_sets) {
    if (set.pending_boundary_bank != nullptr || m_ctrl->m_clk < set.next_block_allowed_clk) {
      return false;
    }
  }

  for (int scope = 0; scope < m_scope_count; scope++) {
    m_pending_refpbs.push_back(m_sets[scope].banks_by_flat[m_next_flat_bank]);
  }
  m_next_flat_bank = (m_next_flat_bank + 1) % m_banks_per_scope;
  return true;
}

bool HBMPerBankRefreshCore::service_pending_refpb() {
  if (m_pending_refpbs.empty()) {
    return false;
  }

  auto pending = m_pending_refpbs.front();
  Request req(build_addr_vec(pending.node), Request::Cmd, m_cmd_refpb);
  if (!m_ctrl->priority_send(req)) {
    return true;
  }

  auto& set = m_sets[pending.scope];
  m_pending_refpbs.pop_front();

  if ((pending.flat_bank + 1) % m_boundary_block_size == 0) {
    set.pending_boundary_bank = pending.node;
    set.pending_boundary_enqueue_clk = m_ctrl->m_clk;
  }

  return true;
}

void HBMPerBankRefreshCore::tick() {
  observe_boundary_refpbs();

  if (service_pending_refpb()) {
    return;
  }

  if (m_ctrl->m_clk < m_next_refresh_clk) {
    return;
  }

  if (!seed_pending_refpbs()) {
    // Retain the absolute tREFIpb deadline so legal commands after the
    // boundary pause recover the required average cadence.
    return;
  }

  service_pending_refpb();
  m_next_refresh_clk += m_nrefipb;
}

class HBMPerBankRefresh final : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, HBMPerBankRefresh, "HBMPerBankRefresh")

 private:
  HBMPerBankRefreshCore m_core;

  void init() override {
    m_core.init(cast_parent<ControllerBase>());
  }

  void tick() override {
    m_core.tick();
  }
};

class HBM34PerBankRefresh final : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, HBM34PerBankRefresh, "HBM34PerBankRefresh")

 private:
  HBMPerBankRefreshCore m_core;

  void init() override {
    m_core.init(cast_parent<ControllerBase>());
  }

  void tick() override {
    m_core.tick();
  }
};

}  // namespace Ramulator
