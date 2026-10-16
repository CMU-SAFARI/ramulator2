#include <stdexcept>
#include <string>

#include "ramulator/base/base.h"
#include "ramulator/controller/impl/lpddr_controller_base.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class LPDDR6Controller final : public LPDDRControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, LPDDR6Controller, LPDDRControllerBase, "LPDDR6")

 public:
  void init() override {
    LPDDRControllerBase::init();

    const auto& spec = *m_device.m_spec;
    m_cmd_act1 = spec.get_command_id("ACT1");
    m_cmd_act2 = spec.get_command_id("ACT2");
    m_cmd_cas = spec.get_command_id("CAS");
    m_cmd_rd = spec.get_command_id("RD_S");
    m_cmd_wr = spec.get_command_id("WR_S");
    m_cmd_rda = spec.get_command_id("RDA_S");
    m_cmd_wra = spec.get_command_id("WRA_S");
    m_cmd_rd_l = spec.get_command_id("RD_L");
    m_cmd_wr_l = spec.get_command_id("WR_L");
    m_cmd_rda_l = spec.get_command_id("RDA_L");
    m_cmd_wra_l = spec.get_command_id("WRA_L");

    m_nAAD = spec.get_timing_value("nAAD");
    m_read_latency = spec.get_timing_value("nRL");
    m_write_latency = spec.get_timing_value("nWL");
    m_burst_cycles = spec.get_timing_value("nBL_min");
    // BL48 WCK toggles through both 24-beat segments and the gap between them,
    // so the long-burst WCK-expiry window is BL/n_min(BL48), not BL/n_max.
    m_burst_cycles_long = spec.get_timing_value("nBL_min_L");
    m_nWCKPST = spec.get_timing_value("nWCKPST");

    // RD/WR carry WS (JESD209-6 §7.3.3); WCK always-on mode keeps WCK synced (§7.3.4).
    m_access_wck_sync = true;
    const std::string& wck_mode = spec.mode("wck_mode");
    if (wck_mode != "sync" && wck_mode != "always_on") {
      throw std::runtime_error("LPDDR6Controller: invalid wck_mode '" + wck_mode + "'");
    }
    m_wck_sync_mode = wck_mode == "always_on" ? WCKSyncMode::AlwaysOn : WCKSyncMode::NeedSync;
  }
};

}  // namespace Ramulator
