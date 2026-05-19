#include "ramulator/controller/impl/hbm_controller_base.h"

#include <stdexcept>
#include <string>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"

namespace Ramulator {

class GDDR7Controller final : public HBMControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, GDDR7Controller, HBMControllerBase, "GDDR7")

 private:
  std::string m_rck_mode = "always_on";

 public:
  void init() override {
    HBMControllerBase::init();

    RAMULATOR_PARSE_PARAM(m_rck_mode, std::string, "rck_mode").default_val("always_on");
    if (m_rck_mode != "always_on") {
      throw std::runtime_error("GDDR7: only rck_mode='always_on' is supported in the initial implementation");
    }
  }
};

}  // namespace Ramulator
