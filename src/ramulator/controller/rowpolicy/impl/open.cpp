#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

// Open row policy — keeps rows open after access (all hooks are no-ops).
class OpenRowPolicy : public IRowPolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRowPolicy, OpenRowPolicy, "Open")

  ControllerBase* m_ctrl;

  void init() override {
    m_ctrl = cast_parent<ControllerBase>();
  }
};

}  // namespace Ramulator
