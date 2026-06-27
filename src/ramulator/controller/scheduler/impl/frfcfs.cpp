#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/scheduler/i_scheduler.h"

namespace Ramulator {

// First-Ready First-Come-First-Served scheduler.
// Prioritizes requests whose command timing is ready; breaks ties by arrival order.
class FRFCFSScheduler : public IScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IScheduler, FRFCFSScheduler, "FRFCFS")

  ControllerBase* m_ctrl;

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
  }

  ReqBuffer::iterator get_best_request(ReqBuffer& buffer, RequestFilterRef filter) override {
    if (buffer.size() == 0) {
      return buffer.end();
    }

    auto candidate = buffer.end();
    bool cand_timing_ok = false;

    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      // Derive the current command (prerequisite resolution)
      it->command = m_ctrl->get_preq_command(it->final_command, it->addr_vec);

      // Apply eligibility filter
      if (filter && !filter(*it)) {
        continue;
      }

      // First eligible candidate — take it, check timing once
      if (candidate == buffer.end()) {
        candidate = it;
        cand_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
        continue;
      }

      // Compare challenger against incumbent (incumbent timing is cached)
      bool it_timing_ok = m_ctrl->check_timing(it->command, it->addr_vec);
      if (cand_timing_ok != it_timing_ok) {
        if (it_timing_ok) {
          candidate = it;
          cand_timing_ok = true;
        }
      } else if (it->arrive < candidate->arrive) {
        candidate = it;
        // cand_timing_ok unchanged — both had same readiness
      }
    }

    return candidate;
  }
};

}  // namespace Ramulator
