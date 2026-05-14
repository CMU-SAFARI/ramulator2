#include "ramulator/controller/impl/hbm_controller_base.h"

#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

void HBMControllerBase::init() {
  init_base();
}

void HBMControllerBase::setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
  setup_base(frontend, memory_system);
}

void HBMControllerBase::tick() {
  hbm_tick_prologue();
  try_issue_slot(SlotType::ColumnBus);
  try_issue_slot(SlotType::RowBus);
  hbm_tick_epilogue();
}

bool HBMControllerBase::slot_matches(const Request& req, SlotType slot) const {
  const auto& meta = m_device.m_spec->command_meta[req.command];
  return (slot == SlotType::ColumnBus) ? meta.is_column_command : meta.is_row_command;
}

void HBMControllerBase::hbm_tick_prologue() {
  tick_prologue();
  m_refresh->tick();

  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }
}

void HBMControllerBase::hbm_tick_epilogue() {
  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

std::optional<HBMControllerBase::IssuedCommand> HBMControllerBase::try_issue_slot(SlotType slot) {
  Candidate cand;
  if (slot == SlotType::ColumnBus) {
    cand = pick_best_ready_from(m_active_buffer, [&](const Request& req) {
      return slot_matches(req, slot);
    });
  }
  if (!cand.valid) {
    cand = pick_priority_if([&](const Request& req) {
      return slot_matches(req, slot);
    });
  }
  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if([&](const Request& req) {
      return slot_matches(req, slot);
    });
  }

  if (!cand.valid) {
    return std::nullopt;
  }

  int original_cmd = cand.it->command;
  m_rowpolicy->try_upgrade_command(*cand.it);
  if (cand.it->command != original_cmd && !slot_matches(*cand.it, slot)) {
    cand.it->command = original_cmd;
  }

  if (!cand.it->is_stat_updated) {
    update_request_stats(cand.it);
  }

  m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);
  IssuedCommand issued{slot, cand.it->command, cand.it->addr_vec, m_clk};

  m_rowpolicy->on_issue(*cand.it);
  for (auto* p : m_plugins) {
    p->on_issue(*cand.it);
  }

  if (cand.it->command == cand.it->final_command) {
    retire_request(cand.it, *cand.buffer);
  } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
    promote_to_active(cand.it, *cand.buffer);
  }

  return issued;
}

}  // namespace Ramulator
