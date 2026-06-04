#include "ramulator/controller/impl/hbm_controller_base.h"

#include <optional>
#include <stdexcept>
#include <string>
#include "ramulator/base/request.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/frontend/i_frontend.h"

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"

namespace Ramulator {

class GDDR7Controller final : public HBMControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, GDDR7Controller, HBMControllerBase, "GDDR7")

 private:
  // std::string m_rck_mode = "always_on";
  // MR9 OP[1:0] selects which RCK mode is active. Mode is fixed at config time;
  // mid-simulation MRS-driven mode switching is not modeled.
  enum class RCKMode {
    Disabled,         // MR9 OP[1:0] = 00B — RCK signals High-Z; RCKSTRT/RCKSTOP illegal.
    AlwaysOn,         // MR9 OP[1:0] = 11B — RCK toggles at WCK rate continuously.
    StartWithRead,    // MR9 OP[1:0] = 01B — RCK starts on any RD/RDA; RCKSTRT illegal.
    StartWithRCKStrt  // MR9 OP[1:0] = 10B — RCK starts only on explicit RCKSTRT.
  };
  enum class RCKState { Stopped, Toggling };

  std::string m_rck_mode_str = "always_on";
  RCKMode m_rck_mode = RCKMode::AlwaysOn;
  RCKState m_rck_state = RCKState::Stopped;
  int m_rck_idle_threshold = 32;
  Clk_t m_last_rd_clk = -1;

  int m_cmd_rckstrt = -1;
  int m_cmd_rckstop = -1;
  int m_cmd_rd = -1;
  int m_cmd_rda = -1;
  int m_channel_level = -1;

  size_t s_num_rckstrt_issued = 0;
  size_t s_num_rckstop_issued = 0;


 public:
  void init() override {
    HBMControllerBase::init();

    // RAMULATOR_PARSE_PARAM(m_rck_mode, std::string, "rck_mode").default_val("always_on");
    // if (m_rck_mode != "always_on") {
    //   throw std::runtime_error("GDDR7: only rck_mode='always_on' is supported in the initial implementation");

    RAMULATOR_PARSE_PARAM(m_rck_mode_str, std::string, "rck_mode").default_val("always_on");
    RAMULATOR_PARSE_PARAM(m_rck_idle_threshold, int, "rck_idle_threshold").default_val(32);

    if (m_rck_mode_str == "disabled") {
      m_rck_mode = RCKMode::Disabled;
    } else if (m_rck_mode_str == "always_on") {
      m_rck_mode = RCKMode::AlwaysOn;
    } else if (m_rck_mode_str == "start_with_read") {
      m_rck_mode = RCKMode::StartWithRead;
    } else if (m_rck_mode_str == "start_with_rckstrt") {
      m_rck_mode = RCKMode::StartWithRCKStrt;
    } else {
      throw std::runtime_error(
          "GDDR7: rck_mode='" + m_rck_mode_str +
          "' is not supported. Allowed values: 'disabled', 'always_on', 'start_with_read', 'start_with_rckstrt'");
    }

    if (m_rck_idle_threshold < 0) {
      throw std::runtime_error("GDDR7: rck_idle_threshold must be non-negative");
    }

    const auto& spec = *m_device.m_spec;
    m_cmd_rckstrt = spec.get_command_id("RCKSTRT");
    m_cmd_rckstop = spec.get_command_id("RCKSTOP");
    m_cmd_rd = spec.get_command_id("RD");
    m_cmd_rda = spec.get_command_id("RDA");
    m_channel_level = spec.get_level_id("Channel");

    // AlwaysOn = the clock toggles unconditionally; Disabled = it never does.
    // Both render the state machine inert. The two start/stop modes drive it.
    m_rck_state = (m_rck_mode == RCKMode::AlwaysOn) ? RCKState::Toggling : RCKState::Stopped;
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    HBMControllerBase::setup(frontend, memory_system);
    m_stats.add("num_rckstrt_issued", s_num_rckstrt_issued);
    m_stats.add("num_rckstop_issued", s_num_rckstop_issued);
  }

  bool priority_send(Request& req) override {
    const bool is_rck = (req.final_command == m_cmd_rckstrt || req.final_command == m_cmd_rckstop);
    if (is_rck && (m_rck_mode == RCKMode::Disabled || m_rck_mode == RCKMode::AlwaysOn)) {
      throw std::runtime_error(
          "GDDR7: RCKSTRT/RCKSTOP are illegal in rck_mode='" + m_rck_mode_str + "'");
    }
    if (req.final_command == m_cmd_rckstrt && m_rck_mode == RCKMode::StartWithRead) {
      throw std::runtime_error(
          "GDDR7: RCKSTRT is illegal in rck_mode='start_with_read' (reads implicitly start RCK)");
    }
    return HBMControllerBase::priority_send(req);
  }

  void tick() override {
    hbm_tick_prologue();
    manage_rck();
    auto col = try_issue_slot(SlotType::ColumnBus);
    auto row = try_issue_slot(SlotType::RowBus);
    observe_issued(col);
    observe_issued(row);
    hbm_tick_epilogue();
  }

 private:
  AddrVec_t make_channel_scope_addr_vec() {
    AddrVec_t vec(m_device.m_spec->level_count, -1);
    vec[m_channel_level] = m_channel_id;
    return vec;
  }

  bool any_reads_inflight() const {
    if (m_read_buffer.size() > 0) {
      return true;
    }
    for (const auto& req : const_cast<ReqBuffer&>(m_active_buffer)) {
      if (req.type_id == Request::Type::Read) {
        return true;
      }
    }
    return false;
  }

  bool priority_buffer_contains(int cmd_id) const {
    for (const auto& req : const_cast<ReqBuffer&>(m_priority_buffer)) {
      if (req.final_command == cmd_id) {
        return true;
      }
    }
    return false;
  }

  void inject_rck_command(int cmd_id) {
    Request req(make_channel_scope_addr_vec(), Request::Cmd, cmd_id);
    HBMControllerBase::priority_send(req);
  }

  // In start/stop modes: pre-arm RCKSTRT when a read is waiting on a stopped
  // clock; inject RCKSTOP after the read stream has been quiet long enough.
  // Device-side timing constraints (nRCKSTRT2RD, nRD2RCKSTOP, nRCKSP2ST,
  // nRCKST2SP) enforce all spacing — manage_rck() only decides *whether*
  // to enqueue a command, not *when* it issues.
  void manage_rck() {
    if (m_rck_mode == RCKMode::Disabled || m_rck_mode == RCKMode::AlwaysOn) {
      return;
    }

    if (m_rck_mode == RCKMode::StartWithRCKStrt && m_rck_state == RCKState::Stopped &&
        m_read_buffer.size() > 0 && !priority_buffer_contains(m_cmd_rckstrt)) {
      inject_rck_command(m_cmd_rckstrt);
    }

    if (m_rck_state == RCKState::Toggling && m_last_rd_clk >= 0 && !any_reads_inflight() &&
        (m_clk - m_last_rd_clk) >= m_rck_idle_threshold && !priority_buffer_contains(m_cmd_rckstop)) {
      inject_rck_command(m_cmd_rckstop);
    }
  }

  void observe_issued(const std::optional<IssuedCommand>& cmd) {
    if (!cmd) {
      return;
    }
    if (cmd->command == m_cmd_rckstrt) {
      m_rck_state = RCKState::Toggling;
      s_num_rckstrt_issued++;
    } else if (cmd->command == m_cmd_rckstop) {
      m_rck_state = RCKState::Stopped;
      s_num_rckstop_issued++;
    } else if (cmd->command == m_cmd_rd || cmd->command == m_cmd_rda) {
      // In StartWithRead, a read is the implicit RCK start trigger.
      if (m_rck_mode == RCKMode::StartWithRead && m_rck_state == RCKState::Stopped) {
        m_rck_state = RCKState::Toggling;
      }
      m_last_rd_clk = m_clk;
    }
  }
};

}  // namespace Ramulator
