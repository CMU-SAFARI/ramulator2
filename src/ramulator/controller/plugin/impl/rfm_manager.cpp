#include <stdexcept>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

// Refresh-Management (RFM) plugin — per-bank ACT-count tracker that
// queues an RFMpb (refresh-management per-bank) command whenever a
// bank's running ACT count crosses a configurable threshold.
//
// Models the JEDEC-defined RFM hammer-mitigation hook on HBM3, HBM4,
// and any future DDR/LPDDR variant that exposes RFMpb in its
// `commands` list. The DRAM standard already declares the timing
// edges that govern RFMpb scheduling (e.g. nRFMpb, nRREFD,
// ACT<->RFMpb tRRD edges); this plugin just decides *when* an RFMpb
// is needed.
//
// Counting policy:
//   - Increment per-bank counter on every issued ACT.
//   - When a bank's count reaches `rfm_thresh`, mark it pending.
//   - In pre_schedule(), priority_send() one RFMpb per pending bank,
//     subject to the controller's priority-buffer capacity. If the
//     buffer is full, retry on a later tick (the bank stays pending).
//   - When the queued RFMpb actually issues (visible via on_issue()),
//     reset the counter and clear pending.
//
// Example config (Python):
//   ramulator.controller_plugin.RFMManager(rfm_thresh=80)
class RFMManager : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, RFMManager, "RFMManager")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_rfm_thresh, int, "rfm_thresh").default_val(80);
    if (m_rfm_thresh <= 0) {
      throw std::runtime_error("RFMManager: rfm_thresh must be > 0");
    }
  }

  void setup(IFrontEnd* /*frontend*/, IMemorySystem* /*memory_system*/) override {
    m_ctrl = cast_parent<ControllerBase>();
    const auto& spec = *m_ctrl->m_device.m_spec;

    if (!spec.has_command("RFMpb")) {
      throw std::runtime_error(
          "RFMManager: DRAM standard '" + spec.standard_name +
          "' does not declare RFMpb (RFM is HBM3/HBM4 / DDR5-RFM territory).");
    }
    if (!spec.has_command("ACT")) {
      throw std::runtime_error(
          "RFMManager: DRAM standard '" + spec.standard_name +
          "' does not declare ACT — cannot count activations.");
    }

    m_cmd_act = spec.get_command_id("ACT");
    m_cmd_rfmpb = spec.get_command_id("RFMpb");

    int num_banks = static_cast<int>(m_ctrl->m_device.m_bank_nodes.size());
    m_bank_act_counters.assign(num_banks, 0);
    m_rfm_queued.assign(num_banks, false);

    m_stats.add("rfm_issued", s_rfm_issued);
    m_stats.add("rfm_pending_peak", s_rfm_pending_peak);
  }

  void pre_schedule() override {
    int pending_now = 0;
    for (int bank_id = 0; bank_id < static_cast<int>(m_bank_act_counters.size()); ++bank_id) {
      if (m_rfm_queued[bank_id]) {
        ++pending_now;
        continue;
      }
      if (m_bank_act_counters[bank_id] < m_rfm_thresh) {
        continue;
      }

      Request rfm = build_rfm_request(bank_id);
      if (m_ctrl->priority_send(rfm)) {
        m_rfm_queued[bank_id] = true;
        ++pending_now;
      }
      // priority_send false -> buffer full; retry next tick. Counter stays
      // above threshold so we'll attempt again. We deliberately do NOT
      // count buffer-full as an issued RFM.
    }
    if (pending_now > s_rfm_pending_peak) {
      s_rfm_pending_peak = pending_now;
    }
  }

  void on_issue(const Request& req) override {
    if (req.command == m_cmd_act) {
      int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
      ++m_bank_act_counters[bank_id];
    } else if (req.command == m_cmd_rfmpb) {
      int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
      m_bank_act_counters[bank_id] = 0;
      m_rfm_queued[bank_id] = false;
      ++s_rfm_issued;
    }
  }

  void reset_stats() override {
    s_rfm_issued = 0;
    s_rfm_pending_peak = 0;
  }

 private:
  Request build_rfm_request(int flat_bank_id) const {
    DRAMNode* node = m_ctrl->m_device.m_bank_nodes[flat_bank_id];
    AddrVec_t addr_vec(m_ctrl->m_device.m_spec->level_count, -1);
    for (auto* n = node; n != nullptr; n = n->m_parent_node) {
      addr_vec[n->m_level] = n->m_node_id;
    }
    return Request(std::move(addr_vec), Request::Cmd, m_cmd_rfmpb);
  }

  ControllerBase* m_ctrl = nullptr;
  int m_cmd_act = -1;
  int m_cmd_rfmpb = -1;
  int m_rfm_thresh = 80;

  std::vector<int> m_bank_act_counters;
  std::vector<bool> m_rfm_queued;

  size_t s_rfm_issued = 0;
  size_t s_rfm_pending_peak = 0;
};

}  // namespace Ramulator
