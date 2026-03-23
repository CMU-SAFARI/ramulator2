#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"

namespace Ramulator {

class GenericDDRController : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, GenericDDRController, ControllerBase, "GenericDDR")

 public:
  void init() override {
    init_base();
  }
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
    setup_base(frontend, memory_system);
  }
  void tick() override;
};

void GenericDDRController::tick() {
  // Common bookkeeping: clk advance, req queue stats update, completed reads draining
  tick_prologue();

  // We give refresh requests high priority in the same tick
  m_refresh->tick();

  // Pre-schedule hooks
  m_rowpolicy->pre_schedule();  // e.g., CloseRow policy may inject PREpb here
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  // Try to find a candidate request to schedule
  // Priority: active > priority > read/write
  // 1. Try to schedule from active
  Candidate cand = pick_best_ready_from(m_active_buffer, {});

  // 2. If no candidate found, try to schedule from priority
  if (!cand.valid) {
    cand = pick_priority_if();
  }

  // 3. If no candidate found, try to schedule from read/write (with write mode check)
  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if();
  }

  // We have a valid request to serve this cycle
  if (cand.valid) {
    // Rowpolicy *may* upgrade the command to AutoPrecharge version
    m_rowpolicy->try_upgrade_command(*cand.it);

    // Issue command to DRAM device
    m_device.issue_command(cand.it->command, cand.it->addr_vec, m_clk);

    if (!cand.it->is_stat_updated) {
      update_request_stats(cand.it);
    }

    // Notify row policy and plugins of the issued command
    m_rowpolicy->on_issue(*cand.it);
    for (auto* p : m_plugins) {
      p->on_issue(*cand.it);
    }

    // Advance request
    if (cand.it->command == cand.it->final_command) {
      retire_request(cand.it, *cand.buffer);
    } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
      promote_to_active(cand.it, *cand.buffer);
    }
  }

  // Post-schedule hooks
  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

}  // namespace Ramulator
