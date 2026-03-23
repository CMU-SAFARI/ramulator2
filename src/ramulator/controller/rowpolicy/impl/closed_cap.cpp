#include <vector>

#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

// Closed row policy with CAP — close rows after N column accesses.
//
// After CAP is reached, the policy opportunistically rewrites RD → RDA / WR → WRA
// (auto-precharge). If AP commands aren't available or timing isn't ready, pre_schedule()
// injects explicit PREpb as a fallback.
class ClosedCAPRowPolicy : public IRowPolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRowPolicy, ClosedCAPRowPolicy, "ClosedCAP")

  ControllerBase* m_ctrl = nullptr;
  DRAMDevice* m_device = nullptr;
  const DRAMSpec* m_spec = nullptr;

  int m_cap = 4;
  int m_cmd_prepb = -1;
  int m_cmd_rd = -1;
  int m_cmd_wr = -1;
  int m_cmd_rda = -1;  // -1 if standard doesn't define RDA
  int m_cmd_wra = -1;  // -1 if standard doesn't define WRA
  bool m_has_ap = false;

  std::vector<int> m_col_accesses;     // Per flat-bank access count
  std::vector<bool> m_prepb_injected;  // Per flat-bank PREpb pending flag

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
    m_device = &m_ctrl->m_device;
    m_spec = m_device->m_spec;
    RAMULATOR_PARSE_PARAM(m_cap, int, "cap").default_val(4);

    m_cmd_prepb = m_spec->get_command_id("PREpb");
    m_cmd_rd = m_spec->get_command_id("RD");
    m_cmd_wr = m_spec->get_command_id("WR");
    m_cmd_rda = m_spec->has_command("RDA") ? m_spec->get_command_id("RDA") : -1;
    m_cmd_wra = m_spec->has_command("WRA") ? m_spec->get_command_id("WRA") : -1;
    m_has_ap = (m_cmd_rda != -1 && m_cmd_wra != -1);

    int num_banks = static_cast<int>(m_device->m_bank_nodes.size());
    m_col_accesses.assign(num_banks, 0);
    m_prepb_injected.assign(num_banks, false);
  }

  void try_upgrade_command(Request& req) override {
    if (!m_has_ap) {
      return;
    }
    if (req.addr_vec.empty()) {
      return;
    }

    int bank_id = m_device->flat_bank_id(req.addr_vec);
    if (bank_id < 0 || bank_id >= static_cast<int>(m_col_accesses.size())) {
      return;
    }

    if (m_col_accesses[bank_id] < m_cap) {
      return;
    }

    // Opportunistic AP: upgrade RD → RDA or WR → WRA if the standard supports it
    // and the AP command timing is ready. If not, leave req.command unchanged.
    if (req.command == m_cmd_rd && m_ctrl->check_timing(m_cmd_rda, req.addr_vec)) {
      req.command = m_cmd_rda;
      req.final_command = m_cmd_rda;
    } else if (req.command == m_cmd_wr && m_ctrl->check_timing(m_cmd_wra, req.addr_vec)) {
      req.command = m_cmd_wra;
      req.final_command = m_cmd_wra;
    }
  }

  void on_issue(const Request& req) override {
    const auto& meta = m_spec->command_meta[req.command];

    // Closing command (PREpb/PREab/RDA/WRA/etc.) resets CAP tracking for target banks.
    if (meta.is_closing) {
      for (int id : m_device->get_target_banks(req.command, req.addr_vec)) {
        m_col_accesses[id] = 0;
        m_prepb_injected[id] = false;
      }
    }

    // Access command increments CAP counter.
    if (meta.is_accessing && !meta.is_closing && !req.addr_vec.empty()) {
      int bid = m_device->flat_bank_id(req.addr_vec);
      if (bid >= 0 && bid < static_cast<int>(m_col_accesses.size())) {
        m_col_accesses[bid]++;
      }
    }
  }

  void pre_schedule() override {
    // Fallback: if CAP reached and AP hasn't closed the bank, inject explicit PREpb.
    for (int i = 0; i < static_cast<int>(m_col_accesses.size()); i++) {
      if (m_col_accesses[i] < m_cap || m_prepb_injected[i]) {
        continue;
      }

      AddrVec_t addr(m_spec->level_count, -1);
      for (auto* n = m_device->m_bank_nodes[i]; n; n = n->m_parent_node) {
        addr[n->m_level] = n->m_node_id;
      }

      if (!m_ctrl->check_timing(m_cmd_prepb, addr)) {
        continue;
      }

      Request req(std::move(addr), Request::Cmd, m_cmd_prepb);
      if (m_ctrl->priority_send(req)) {
        m_prepb_injected[i] = true;
      }
    }
  }
};

}  // namespace Ramulator
