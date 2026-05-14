#include "ramulator/base/base.h"
#include "ramulator/controller/impl/hbm_controller_base.h"

namespace Ramulator {

class HBM12Controller final : public HBMControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, HBM12Controller, HBMControllerBase, "HBM12")
};

}  // namespace Ramulator
