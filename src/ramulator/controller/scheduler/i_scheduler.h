#ifndef RAMULATOR_CONTROLLER_SCHEDULER_I_SCHEDULER_H
#define RAMULATOR_CONTROLLER_SCHEDULER_I_SCHEDULER_H

#include "ramulator/base/base.h"
#include "ramulator/base/function_ref.h"
#include "ramulator/controller/i_controller.h"

namespace Ramulator {

using RequestFilterRef = FunctionRef<bool(const Request&)>;

// Selects which pending request to issue next from the controller's queue.
class IScheduler {
  RAMULATOR_REGISTER_INTERFACE(IScheduler, "scheduler")
 public:
  // Contract:
  //   - The scheduler derives req.command from req.final_command before
  //     invoking filter, so controller predicates can reason about the current
  //     command without mutating the request.
  //   - The filter is eligibility-only. It must not mutate controller state.
  virtual ReqBuffer::iterator get_best_request(
      ReqBuffer& buffer,
      RequestFilterRef filter) = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_SCHEDULER_I_SCHEDULER_H
