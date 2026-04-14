#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/controller/scheduler/i_scheduler.h"

namespace Ramulator {

// Dual-bus controller for HBM-family DRAM standards (HBM1, HBM2, HBM3).
//
// Unlike GenericDDRController which issues one command per tick, this
// controller issues up to two commands per tick — one on the column bus
// and one on the row bus — exploiting the dual command bus architecture.
class HBMController : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, HBMController, ControllerBase, "HBM")

 public:
  void init() override {
    init_base();
  }
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    setup_base(frontend, memory_system);
  }
  void tick() override;

 private:
  enum class SlotType : int { Column = 0, Row = 1 };

  bool is_col_bus(int cmd);
  bool is_row_bus(int cmd);
  bool slot_matches(int cmd, SlotType slot);
};

void HBMController::tick() {
  tick_prologue();
  m_refresh->tick();

  // Pre-schedule
  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  // Deterministic dual-slot pass: column bus first, then row bus.
  for (SlotType slot : {SlotType::Column, SlotType::Row}) {
    // Candidate selection: active > priority > read/write
    Candidate cand;
    if (slot == SlotType::Column) {
      // Active-buffer requests are post-open column commands, so only the
      // column slot can issue them.
      cand = pick_best_ready_from(m_active_buffer, [&](const Request& req) {
        return slot_matches(req.command, slot);
      });
    }
    if (!cand.valid) {
      cand = pick_priority_if([&](const Request& req) {
        return slot_matches(req.command, slot);
      });
    }
    if (!cand.valid && m_priority_buffer.size() == 0) {
      cand = pick_rw_if([&](const Request& req) {
        return slot_matches(req.command, slot);
      });
    }

    if (!cand.valid) {
      continue;
    }

    // Slot-aware upgrade: revert if rewrite doesn't match this bus slot.
    int original_cmd = cand.it->command;
    m_rowpolicy->try_upgrade_command(*cand.it);
    if (cand.it->command != original_cmd && !slot_matches(cand.it->command, slot)) {
      cand.it->command = original_cmd;
    }

    if (!cand.it->is_stat_updated) {
      update_request_stats(cand.it);
    }

    // Issue to DRAM
    m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);

    // On-issue: observe the command while iterator is still valid
    m_rowpolicy->on_issue(*cand.it);
    for (auto* p : m_plugins) {
      p->on_issue(*cand.it);
    }

    // Advance request lifecycle (may invalidate iterator)
    if (cand.it->command == cand.it->final_command) {
      retire_request(cand.it, *cand.buffer);
    } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
      promote_to_active(cand.it, *cand.buffer);
    }
  }

  // Post-schedule
  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

bool HBMController::is_col_bus(int cmd) {
  return m_device.m_spec->command_meta[cmd].is_column_command;
}

bool HBMController::is_row_bus(int cmd) {
  return m_device.m_spec->command_meta[cmd].is_row_command;
}

bool HBMController::slot_matches(int cmd, SlotType slot) {
  return (slot == SlotType::Column) ? is_col_bus(cmd) : is_row_bus(cmd);
}

}  // namespace Ramulator
