#include "ramulator/controller/impl/hbm_controller_base.h"

#include <optional>
#include <stdexcept>
#include <string>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/base/request.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class GDDR7Controller final : public HBMControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, GDDR7Controller, HBMControllerBase, "GDDR7")

 private:
  enum class RCKMode { AlwaysOn, StartWithRead, StartWithRCKStrt };
  enum class RCKState { Stopped, Toggling };

  std::string m_rck_mode_str = "always_on";
  RCKMode m_rck_mode = RCKMode::AlwaysOn;
  RCKState m_rck_state = RCKState::Toggling;
  int m_rck_idle_threshold = 32;
  Clk_t m_last_read_clk = -1;

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

    RAMULATOR_PARSE_PARAM(m_rck_mode_str, std::string, "rck_mode").default_val("always_on");
    RAMULATOR_PARSE_PARAM(m_rck_idle_threshold, int, "rck_idle_threshold").default_val(32);

    if (m_rck_mode_str == "always_on") {
      m_rck_mode = RCKMode::AlwaysOn;
    } else if (m_rck_mode_str == "start_with_read") {
      m_rck_mode = RCKMode::StartWithRead;
    } else if (m_rck_mode_str == "start_with_rckstrt") {
      m_rck_mode = RCKMode::StartWithRCKStrt;
    } else {
      throw std::runtime_error(
          "GDDR7: unsupported rck_mode '" + m_rck_mode_str +
          "'; allowed values are 'always_on', 'start_with_read', and 'start_with_rckstrt'");
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

    m_rck_state = (m_rck_mode == RCKMode::AlwaysOn) ? RCKState::Toggling : RCKState::Stopped;
  }

  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    HBMControllerBase::setup(frontend, memory_system);

    m_stats.add("num_rckstrt_issued", s_num_rckstrt_issued);
    m_stats.add("num_rckstop_issued", s_num_rckstop_issued);
  }

  void reset_stats() override {
    ControllerBase::reset_stats();

    s_num_rckstrt_issued = 0;
    s_num_rckstop_issued = 0;
  }

  bool priority_send(Request& req) override {
    const bool is_rckstrt = req.final_command == m_cmd_rckstrt;
    const bool is_rckstop = req.final_command == m_cmd_rckstop;
    if (m_rck_mode == RCKMode::AlwaysOn && (is_rckstrt || is_rckstop)) {
      throw std::runtime_error("GDDR7: RCKSTRT/RCKSTOP are not legal when rck_mode='always_on'");
    }
    if (m_rck_mode == RCKMode::StartWithRead && is_rckstrt) {
      throw std::runtime_error("GDDR7: RCKSTRT is not legal when rck_mode='start_with_read'");
    }
    return HBMControllerBase::priority_send(req);
  }

  void tick() override {
    hbm_tick_prologue();
    auto col = try_issue_internal_rck();
    if (!col) {
      col = try_issue_slot(SlotType::ColumnBus);
    }
    auto row = try_issue_slot(SlotType::RowBus);
    update_rck_state_on_issue(col);
    update_rck_state_on_issue(row);
    hbm_tick_epilogue();
  }

 protected:
  bool slot_matches(const Request& req, SlotType slot) const override {
    if (!HBMControllerBase::slot_matches(req, slot)) {
      return false;
    }
    if (m_rck_mode == RCKMode::StartWithRCKStrt && m_rck_state == RCKState::Stopped &&
        is_read_like(req.command)) {
      return false;
    }
    return true;
  }

 private:
  bool is_read_like(int cmd) const {
    return cmd == m_cmd_rd || cmd == m_cmd_rda;
  }

  bool priority_buffer_contains(int cmd) {
    for (auto it = m_priority_buffer.begin(); it != m_priority_buffer.end(); ++it) {
      if (it->final_command == cmd) {
        return true;
      }
    }
    return false;
  }

  bool reads_waiting() {
    if (m_read_buffer.size() > 0) {
      return true;
    }
    for (auto it = m_active_buffer.begin(); it != m_active_buffer.end(); ++it) {
      if (it->type_id == Request::Type::Read) {
        return true;
      }
    }
    return false;
  }

  int get_rck_cmd() {
    if (m_rck_mode == RCKMode::AlwaysOn) {
      return -1;
    }

    if (m_rck_mode == RCKMode::StartWithRCKStrt && m_rck_state == RCKState::Stopped &&
        reads_waiting() && !priority_buffer_contains(m_cmd_rckstrt)) {
      return m_cmd_rckstrt;
    }

    if (m_rck_state == RCKState::Toggling && m_last_read_clk >= 0 && !reads_waiting() &&
        (m_clk - m_last_read_clk) >= m_rck_idle_threshold &&
        !priority_buffer_contains(m_cmd_rckstop)) {
      return m_cmd_rckstop;
    }

    return -1;
  }

  std::optional<IssuedCommand> try_issue_internal_rck() {
    int cmd = get_rck_cmd();
    if (cmd < 0) {
      return std::nullopt;
    }

    AddrVec_t addr_vec(m_device.m_spec->level_count, -1);
    addr_vec[m_channel_level] = m_channel_id;
    if (!check_timing(cmd, addr_vec)) {
      return std::nullopt;
    }

    Request req(addr_vec, Request::Cmd, cmd);
    req.command = cmd;
    m_device.issue_command(cmd, addr_vec, m_clk);
    IssuedCommand issued{SlotType::ColumnBus, cmd, addr_vec, m_clk};

    m_rowpolicy->on_issue(req);
    for (auto* p : m_plugins) {
      p->on_issue(req);
    }

    return issued;
  }

  void update_rck_state_on_issue(const std::optional<IssuedCommand>& issued) {
    if (!issued) {
      return;
    }
    if (issued->command == m_cmd_rckstrt) {
      m_rck_state = RCKState::Toggling;
      s_num_rckstrt_issued++;
    } else if (issued->command == m_cmd_rckstop) {
      m_rck_state = RCKState::Stopped;
      s_num_rckstop_issued++;
    } else if (is_read_like(issued->command)) {
      if (m_rck_mode == RCKMode::StartWithRead && m_rck_state == RCKState::Stopped) {
        m_rck_state = RCKState::Toggling;
      }
      m_last_read_clk = m_clk;
    }
  }
};

}  // namespace Ramulator
