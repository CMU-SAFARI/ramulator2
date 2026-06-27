#include <utility>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/controller/plugin/controller_validation_hook.h"

namespace Ramulator {

class IssuedCommandValidationHook final
    : public IControllerPlugin,
      public IControllerValidationHook,
      public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(
      IControllerPlugin,
      IssuedCommandValidationHook,
      "IssuedCommandValidationHook")

 public:
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
  }

  void pre_schedule() override {
    m_issued_commands_this_tick.clear();
  }

  void on_issue(const Request& req) override {
    m_issued_commands_this_tick.push_back({
        .clk = m_ctrl->m_clk,
        .command = req.command,
        .final_command = req.final_command,
        .addr_vec = req.addr_vec,
        .type_id = req.type_id,
        .source_id = req.source_id,
    });
  }

  std::vector<IssuedCommandRecord> take_issued_commands_this_tick() override {
    return std::exchange(m_issued_commands_this_tick, {});
  }

 private:
  ControllerBase* m_ctrl = nullptr;
  std::vector<IssuedCommandRecord> m_issued_commands_this_tick;

  void init() override {
  }
};

}  // namespace Ramulator
