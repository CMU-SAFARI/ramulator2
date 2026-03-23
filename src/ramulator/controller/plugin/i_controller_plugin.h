#ifndef RAMULATOR_CONTROLLER_PLUGIN_I_CONTROLLER_PLUGIN_H
#define RAMULATOR_CONTROLLER_PLUGIN_I_CONTROLLER_PLUGIN_H

#include "ramulator/base/base.h"
#include "ramulator/base/request.h"

namespace Ramulator {

class ControllerBase;

// Controller plugin interface — observe and react to the controller's command lifecycle.
//
// Three hooks, all with default no-op implementations:
//
//   pre_schedule():           Things that is better to happen in the same tick
//
//   on_issue(req):            Be notified about the command issued to DRAM
//
//   post_schedule():          Things that happens at the end of memory controller tick
class IControllerPlugin {
  RAMULATOR_REGISTER_INTERFACE(IControllerPlugin, "controller_plugin")
 public:
  virtual void pre_schedule() {
  }
  virtual void on_issue(const Request& req) {
  }
  virtual void post_schedule() {
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_PLUGIN_I_CONTROLLER_PLUGIN_H
