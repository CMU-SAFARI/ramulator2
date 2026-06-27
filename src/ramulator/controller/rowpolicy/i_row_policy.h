#ifndef RAMULATOR_CONTROLLER_ROWPOLICY_I_ROW_POLICY_H
#define RAMULATOR_CONTROLLER_ROWPOLICY_I_ROW_POLICY_H

#include "ramulator/base/base.h"
#include "ramulator/controller/i_controller.h"

namespace Ramulator {

// Row policy interface — decides when to close open rows.
//
// Follows the same 3-phase lifecycle as IControllerPlugin, plus try_upgrade_command:
//
//   pre_schedule():           Things that is better to happen in the same tick
//                             Examples: injecting PREpb.
//
//   try_upgrade_command(req): After candidate selection, before DRAM issue.
//                             May upgrade req.command in-place (e.g., RD → RDA).
//                             Must validate readiness; leave unchanged if not ready.
//
//   on_issue(req):            Be notified about the command issued to DRAM
//                             Examples: Update bank col access counters, counter resets on close.
//
//   post_schedule():          Things that happens at the end of memory controller tick
class IRowPolicy {
  RAMULATOR_REGISTER_INTERFACE(IRowPolicy, "row_policy")
 public:
  virtual void pre_schedule() {
  }

  virtual void try_upgrade_command(Request& req) {
  }

  virtual void on_issue(const Request& req) {
  }

  virtual void post_schedule() {
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_ROWPOLICY_I_ROW_POLICY_H
