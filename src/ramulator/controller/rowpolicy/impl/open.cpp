#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

// Open row policy — keeps rows open after access (all hooks are no-ops).
class OpenRowPolicy : public IRowPolicy, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRowPolicy, OpenRowPolicy, "Open")

  void init() override {}
};

}  // namespace Ramulator
