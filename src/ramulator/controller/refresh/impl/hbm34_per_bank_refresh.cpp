#include <algorithm>
#include <deque>
#include <stdexcept>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

class HBM34PerBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, HBM34PerBankRefresh, "HBM34PerBankRefresh")

 private:
  struct BankRef {
    DRAMNode* node = nullptr;
    int pc = -1;
    int sid = 0;
    int flat_bank = -1;
  };

  struct RefpbSetState {
    std::vector<BankRef> banks_by_flat;
    std::vector<bool> refreshed;
    Clk_t next_set_allowed_clk = 0;
  };

  struct PendingRefpb {
    DRAMNode* node = nullptr;
    int pc = -1;
    int sid = 0;
    int flat_bank = -1;
  };

  ControllerBase* m_ctrl = nullptr;
  int m_cmd_refpb = -1;
  int m_level_pc = -1;
  int m_level_sid = -1;
  int m_level_bg = -1;
  int m_level_bank = -1;
  int m_bank_level = -1;
  int m_pc_count = 0;
  int m_sid_count = 0;
  int m_bank_groups = 0;
  int m_banks_per_group = 0;
  int m_banks_per_sid = 0;
  int m_nrefipb = -1;
  int m_nrfc_pb = -1;
  int m_next_sid = 0;
  int m_next_flat_bank = 0;
  Clk_t m_next_refresh_clk = -1;

  std::vector<std::vector<RefpbSetState>> m_sets;
  std::deque<PendingRefpb> m_pending_refpbs;

  AddrVec_t build_addr_vec(DRAMNode* node) const;
  int flat_bank_in_sid(const AddrVec_t& addr_vec) const;
  void build_refpb_sets();
  bool seed_pending_refpbs();
  bool service_pending_refpb();
  void advance_logical_cursor();

 public:
  void init() override;
  void tick() override;
};

AddrVec_t HBM34PerBankRefresh::build_addr_vec(DRAMNode* node) const {
  AddrVec_t addr_vec(m_ctrl->m_device.m_spec->level_count, -1);
  for (auto* n = node; n != nullptr; n = n->m_parent_node) {
    addr_vec[n->m_level] = n->m_node_id;
  }
  return addr_vec;
}

int HBM34PerBankRefresh::flat_bank_in_sid(const AddrVec_t& addr_vec) const {
  return addr_vec[m_level_bg] * m_banks_per_group + addr_vec[m_level_bank];
}

void HBM34PerBankRefresh::init() {
  m_ctrl = cast_parent<ControllerBase>();
  const auto& info = *m_ctrl->m_device.m_spec;

  m_cmd_refpb = info.get_command_id("REFpb");
  m_level_pc = info.get_level_id("PseudoChannel");
  m_level_sid = info.has_level("Sid") ? info.get_level_id("Sid") : -1;
  m_level_bg = info.get_level_id("BankGroup");
  m_level_bank = info.get_level_id("Bank");
  m_bank_level = info.get_level_id("Bank");
  m_pc_count = info.get_level_size("PseudoChannel");
  m_sid_count = m_level_sid >= 0 ? info.get_level_size("Sid") : 1;
  m_bank_groups = info.get_level_size("BankGroup");
  m_banks_per_group = info.get_level_size("Bank");
  m_banks_per_sid = m_bank_groups * m_banks_per_group;
  m_nrefipb = info.get_timing_value("nREFIpb");
  m_nrfc_pb = info.get_timing_value("nRFCpb");
  m_next_refresh_clk = m_nrefipb;

  build_refpb_sets();
}

void HBM34PerBankRefresh::build_refpb_sets() {
  m_sets.assign(m_pc_count, std::vector<RefpbSetState>(m_sid_count));
  for (auto& pc_sets : m_sets) {
    for (auto& set : pc_sets) {
      set.banks_by_flat.resize(m_banks_per_sid);
      set.refreshed.assign(m_banks_per_sid, false);
    }
  }

  m_ctrl->m_device.m_root->for_each_at_level(m_bank_level, [&](DRAMNode* node) {
    AddrVec_t addr_vec = build_addr_vec(node);
    int pc = addr_vec[m_level_pc];
    int sid = m_level_sid >= 0 ? addr_vec[m_level_sid] : 0;
    int flat_bank = flat_bank_in_sid(addr_vec);

    auto& slot = m_sets[pc][sid].banks_by_flat[flat_bank];
    if (slot.node != nullptr) {
      throw std::runtime_error("HBM34PerBankRefresh found duplicate bank node in a REFpb set");
    }
    slot = BankRef{node, pc, sid, flat_bank};
  });

  for (int pc = 0; pc < m_pc_count; pc++) {
    for (int sid = 0; sid < m_sid_count; sid++) {
      for (int flat_bank = 0; flat_bank < m_banks_per_sid; flat_bank++) {
        if (m_sets[pc][sid].banks_by_flat[flat_bank].node == nullptr) {
          throw std::runtime_error("HBM34PerBankRefresh found an incomplete REFpb set");
        }
      }
    }
  }
}

bool HBM34PerBankRefresh::seed_pending_refpbs() {
  for (int pc = 0; pc < m_pc_count; pc++) {
    auto& set = m_sets[pc][m_next_sid];
    if (m_ctrl->m_clk < set.next_set_allowed_clk) {
      return false;
    }
  }

  for (int pc = 0; pc < m_pc_count; pc++) {
    auto& bank = m_sets[pc][m_next_sid].banks_by_flat[m_next_flat_bank];
    m_pending_refpbs.push_back(PendingRefpb{bank.node, pc, m_next_sid, m_next_flat_bank});
  }

  advance_logical_cursor();
  return true;
}

bool HBM34PerBankRefresh::service_pending_refpb() {
  if (m_pending_refpbs.empty()) {
    return false;
  }

  auto pending = m_pending_refpbs.front();
  Request req(build_addr_vec(pending.node), Request::Cmd, m_cmd_refpb);
  if (!m_ctrl->priority_send(req)) {
    return true;
  }

  auto& set = m_sets[pending.pc][pending.sid];
  set.refreshed[pending.flat_bank] = true;
  m_pending_refpbs.pop_front();

  if (std::all_of(set.refreshed.begin(), set.refreshed.end(), [](bool value) { return value; })) {
    std::fill(set.refreshed.begin(), set.refreshed.end(), false);
    set.next_set_allowed_clk = m_ctrl->m_clk + m_nrfc_pb;
  }

  return true;
}

void HBM34PerBankRefresh::advance_logical_cursor() {
  m_next_flat_bank++;
  if (m_next_flat_bank == m_banks_per_sid) {
    m_next_flat_bank = 0;
    m_next_sid = (m_next_sid + 1) % m_sid_count;
  }
}

void HBM34PerBankRefresh::tick() {
  if (service_pending_refpb()) {
    return;
  }

  if (m_ctrl->m_clk < m_next_refresh_clk) {
    return;
  }

  if (!seed_pending_refpbs()) {
    m_next_refresh_clk = m_ctrl->m_clk + 1;
    return;
  }

  service_pending_refpb();
  m_next_refresh_clk += m_nrefipb;
}

}  // namespace Ramulator
