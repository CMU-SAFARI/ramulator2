#ifndef RAMULATOR_CONTROLLER_PLUGIN_CONTROLLER_VALIDATION_HOOK_H
#define RAMULATOR_CONTROLLER_PLUGIN_CONTROLLER_VALIDATION_HOOK_H

#include <vector>

#include "ramulator/base/type.h"

namespace Ramulator {

struct IssuedCommandRecord {
  Clk_t clk = -1;
  int command = -1;
  int final_command = -1;
  AddrVec_t addr_vec{};
  int type_id = -1;
  int source_id = -1;
};

class IControllerValidationHook {
 public:
  virtual ~IControllerValidationHook() = default;
  virtual std::vector<IssuedCommandRecord> take_issued_commands_this_tick() = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_PLUGIN_CONTROLLER_VALIDATION_HOOK_H
