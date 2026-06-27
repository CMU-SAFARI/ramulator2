#include <algorithm>
#include <vector>

#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/scheduler/i_scheduler.h"

namespace Ramulator {

// FRFCFS with row-hit prioritization.
// Scans the buffer twice: first to identify row-hits
// then do FRFCFS that prevents a row-hit from being preempted by a non-row-hit.
// For example, for DRAM with nRTP < nCCD_L, as long as nRAS has passed,
// accesses to another row will preempt following RDs to the same opened row.
class FRFCFSRowHitScheduler : public IScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FRFCFSRowHitScheduler, "FRFCFS-RowHit")

  ControllerBase* m_ctrl = nullptr;

  std::vector<bool> m_bank_rowhit_flags;
  std::vector<Clk_t> m_bank_rowhit_arrivals;

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
    const size_t num_banks = m_ctrl->m_device.m_bank_nodes.size();
    m_bank_rowhit_flags.assign(num_banks, false);
    m_bank_rowhit_arrivals.assign(num_banks, -1);
  }

  ReqBuffer::iterator get_best_request(ReqBuffer& buffer, RequestFilterRef filter) override {
    if (buffer.size() == 0) {
      return buffer.end();
    }

    std::fill(m_bank_rowhit_flags.begin(), m_bank_rowhit_flags.end(), false);

    // Pass 1: resolve prerequisites and record all row-hits.
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      it->command = m_ctrl->get_preq_command(it->final_command, it->addr_vec);
      
      if (m_ctrl->m_device.check_rowbuffer_hit(it->final_command, it->addr_vec, m_ctrl->m_clk)) {
        const int bank_id = m_ctrl->m_device.get_flat_bank_id(it->addr_vec);
        if (bank_id >= 0 && bank_id < static_cast<int>(m_bank_rowhit_flags.size())) {
          if (!m_bank_rowhit_flags[bank_id] || it->arrive < m_bank_rowhit_arrivals[bank_id]) {
            m_bank_rowhit_flags[bank_id] = true;
            m_bank_rowhit_arrivals[bank_id] = it->arrive;
          }
        }
      }
    }

    // Pass 2: FRFCFS with row-hit preemption blocking.
    auto candidate = buffer.end();
    bool cand_timing_ok = false;

    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      if (filter && !filter(*it)) {
        continue;
      }

      if (would_preempt_rowhit(*it)) {
        continue;
      }

      if (candidate == buffer.end()) {
        candidate = it;
        cand_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
        continue;
      }

      bool it_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
      if (cand_timing_ok != it_timing_ok) {
        if (it_timing_ok) {
          candidate = it;
          cand_timing_ok = true;
        }
      } else if (it->arrive < candidate->arrive) {
        candidate = it;
      }
    }

    return candidate;
  }

  bool would_preempt_rowhit(const Request& req) const {
    const auto& spec = *m_ctrl->m_device.m_spec;
    if (!spec.command_meta[req.command].is_closing) {
      return false;
    }
    if (req.command == req.final_command) {
      return false;
    }

    if (spec.bank_targets[req.command] != BankTarget::Single) {
      return false;
    }

    const int bank_id = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    if (bank_id < 0 || bank_id >= static_cast<int>(m_bank_rowhit_flags.size())) {
      return false;
    }
    if (!m_bank_rowhit_flags[bank_id]) {
      return false;
    }
    return m_bank_rowhit_arrivals[bank_id] <= req.arrive;
  }
};

}  // namespace Ramulator
