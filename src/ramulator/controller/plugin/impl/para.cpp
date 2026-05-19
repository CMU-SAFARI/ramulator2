#include <iostream>
#include <random>
#include <stdexcept>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/plugin/i_controller_plugin.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class PARA : public IControllerPlugin, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, PARA, "PARA")

 private:
  ControllerBase* m_ctrl = nullptr;

  float m_pr_threshold;

  int m_seed;
  std::mt19937 m_generator;
  std::uniform_real_distribution<float> m_distribution;
  bool m_is_debug = false;

  int m_vrr_cmd_id = -1;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_pr_threshold, float, "threshold").required();
    if (m_pr_threshold <= 0.0f || m_pr_threshold >= 1.0f)
      throw std::runtime_error("PARA: Invalid probability threshold!");

    RAMULATOR_PARSE_PARAM(m_seed, int, "seed").default_val(123);
    m_generator = std::mt19937(m_seed);
    m_distribution = std::uniform_real_distribution<float>(0.0, 1.0);

    RAMULATOR_PARSE_PARAM(m_is_debug, bool, "debug").default_val(false);
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    m_ctrl = cast_parent<ControllerBase>();
    auto* spec = m_ctrl->m_device.m_spec;

    if (!spec->has_command("VRR")) {
      throw std::runtime_error(
          "PARA is not compatible with the DRAM standard that does not "
          "have Victim-Row-Refresh (VRR) command!");
    }

    m_vrr_cmd_id = spec->get_command_id("VRR");
  }

  void on_issue(const Request& req) override {
    auto* spec = m_ctrl->m_device.m_spec;
    if (!spec->command_meta[req.command].is_opening) return;
    if (spec->bank_targets[req.command] != BankTarget::Single) return;
    if (m_distribution(m_generator) >= m_pr_threshold) return;

    // Issue VRR with the aggressor's addr_vec;
    Request vrr_req(req.addr_vec, Request::Cmd, m_vrr_cmd_id);
    m_ctrl->priority_send(vrr_req);
    if (m_is_debug) {
      std::cout << "PARA: VRR fired on aggressor addr_vec" << std::endl;
    }
  }
};

}  // namespace Ramulator
