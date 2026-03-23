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

    for (auto& req : buffer) {
      req.command = m_ctrl->get_preq_command(req.final_command, req.addr_vec);
    }

    auto candidate = buffer.end();
    for (auto it = buffer.begin(); it != buffer.end(); it++) {
      if (filter && !filter(*it)) {
        continue;
      }
      if (candidate == buffer.end()) {
        candidate = it;
        continue;
      }
      candidate = compare_native(candidate, it);
    }

    return candidate;
  }

 private:
  ReqBuffer::iterator compare_native(ReqBuffer::iterator req1, ReqBuffer::iterator req2) {
    bool timing_ok1 = m_ctrl->check_timing(req1->command, req1->addr_vec);
    bool timing_ok2 = m_ctrl->check_timing(req2->command, req2->addr_vec);

    if (timing_ok1 ^ timing_ok2) {
      return timing_ok1 ? req1 : req2;
    }

    // Fallback to FCFS
    return (req1->arrive <= req2->arrive) ? req1 : req2;
  }
};

}  // namespace Ramulator
