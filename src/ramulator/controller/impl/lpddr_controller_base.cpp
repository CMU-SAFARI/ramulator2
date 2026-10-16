#include <cassert>
#include <fmt/format.h>
#include <stdexcept>
#include <string>

#include "ramulator/base/base.h"
#include "ramulator/controller/impl/lpddr_controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/controller/scheduler/i_scheduler.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

void LPDDRControllerBase::init() {
  init_base();

  std::string wck_sync_mode_str;
  RAMULATOR_PARSE_PARAM(wck_sync_mode_str, std::string, "wck_sync_mode").default_val("need_sync");
  m_wck_sync_mode = parse_wck_sync_mode(wck_sync_mode_str);

  m_activating_buffer.max_size = m_device.m_bank_nodes.size();
  m_act2_owner_valid.assign(m_device.m_bank_nodes.size(), false);
  m_act2_deadline.assign(m_device.m_bank_nodes.size(), -1);
  m_rank_level = m_device.m_spec->get_level_id("Rank");
  m_wck_expiry.assign(m_device.m_spec->get_level_size("Rank"), 0);
}

void LPDDRControllerBase::setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
  setup_base(frontend, memory_system);

  m_stats.add("cas_issued", s_cas_issued);
  m_stats.add("cas_skipped", s_cas_skipped);
  m_stats.add("act2_deadline_forced", s_act2_deadline_forced);
  m_stats.add("act2_deferred", s_act2_deferred);
}

void LPDDRControllerBase::reset_stats() {
  ControllerBase::reset_stats();
  s_cas_issued = 0;
  s_cas_skipped = 0;
  s_act2_deadline_forced = 0;
  s_act2_deferred = 0;
}

void LPDDRControllerBase::tick() {
  tick_prologue();
  m_refresh->tick();

  m_rowpolicy->pre_schedule();
  for (auto* p : m_plugins) {
    p->pre_schedule();
  }

  Candidate urgent_act2 = pick_urgent_act2();

  // CAS continuation: last cycle issued CAS, now issue the RD/WR.
  if (m_cas_issued) {
    assert(!urgent_act2.valid);
    assert(m_cas_buffer != nullptr);

    if (check_timing(m_cas_req_it->command, m_cas_req_it->addr_vec)) {
      m_cas_issued = false;
      if (m_wck_sync_mode == WCKSyncMode::NeedSync) {
        extend_wck_expiry(m_cas_req_it->command, m_cas_req_it->addr_vec);
      }
      s_cas_issued++;

      if (!m_cas_req_it->is_stat_updated) {
        update_request_stats(m_cas_req_it);
      }

      m_device.issue_command(m_cas_req_it->command, m_cas_req_it->addr_vec, m_clk);

      m_rowpolicy->on_issue(*m_cas_req_it);
      for (auto* p : m_plugins) {
        p->on_issue(*m_cas_req_it);
      }

      if (m_cas_req_it->command == m_cas_req_it->final_command) {
        retire_request(m_cas_req_it, *m_cas_buffer);
      }
    }

    m_rowpolicy->post_schedule();
    for (auto* p : m_plugins) {
      p->post_schedule();
    }
    return;
  }

  Candidate cand = urgent_act2.valid ? urgent_act2 : select_normal_candidate();
  if (!cand.valid) {
    cand = pick_cas_lookahead_candidate();
  }

  // A timing-ready owned ACT2 goes ahead of any non-column command (tRRD/tRCD start at ACT-2, JESD209-5C §7.3.1).
  Act2IssueKind act2_kind = Act2IssueKind::Urgent;
  if (!urgent_act2.valid && (!cand.valid || !(is_access_cmd(cand.it->command) || is_cas_cmd(cand.it->command)))) {
    Candidate deferred = pick_deferred_act2();
    if (deferred.valid) {
      cand = deferred;
      act2_kind = Act2IssueKind::Deferred;
    }
  }

  if (cand.buffer == &m_activating_buffer) {
    issue_owned_act2(cand, act2_kind);
  } else if (cand.valid && !try_issue_cas_sync(cand)) {
    issue_standard_candidate(cand);
  }

  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

bool LPDDRControllerBase::is_access_cmd(int cmd) const {
  return cmd == m_cmd_rd || cmd == m_cmd_wr || cmd == m_cmd_rda || cmd == m_cmd_wra ||
         cmd == m_cmd_rd_l || cmd == m_cmd_wr_l || cmd == m_cmd_rda_l || cmd == m_cmd_wra_l;
}

bool LPDDRControllerBase::is_read_cmd(int cmd) const {
  return cmd == m_cmd_rd || cmd == m_cmd_rda || cmd == m_cmd_rd_l || cmd == m_cmd_rda_l;
}

int LPDDRControllerBase::burst_cycles_for_cmd(int cmd) const {
  if (cmd == m_cmd_rd_l || cmd == m_cmd_wr_l || cmd == m_cmd_rda_l || cmd == m_cmd_wra_l) {
    return m_burst_cycles_long > 0 ? m_burst_cycles_long : m_burst_cycles;
  }
  return m_burst_cycles;
}

LPDDRControllerBase::WCKSyncMode LPDDRControllerBase::parse_wck_sync_mode(const std::string& mode) const {
  if (mode == "need_sync") {
    return WCKSyncMode::NeedSync;
  }
  if (mode == "always_on") {
    return WCKSyncMode::AlwaysOn;
  }

  throw std::runtime_error(
      fmt::format("LPDDRControllerBase: invalid wck_sync_mode '{}'. Expected 'need_sync' or 'always_on'.", mode));
}

bool LPDDRControllerBase::needs_wck_sync(int cmd, const AddrVec_t& addr_vec) const {
  if (!is_access_cmd(cmd)) {
    return false;
  }

  if (m_wck_sync_mode == WCKSyncMode::AlwaysOn) {
    return false;
  }

  return m_clk >= m_wck_expiry[addr_vec[m_rank_level]];
}

void LPDDRControllerBase::extend_wck_expiry(int cmd, const AddrVec_t& addr_vec) {
  int lat = is_read_cmd(cmd) ? m_read_latency : m_write_latency;
  Clk_t exp = m_clk + lat + burst_cycles_for_cmd(cmd) + m_nWCKPST;
  Clk_t& expiry = m_wck_expiry[addr_vec[m_rank_level]];
  if (exp > expiry) {
    expiry = exp;
  }
}

// An access whose WCK2CK sync CAS is not timing-ready this cycle is skipped so it cannot block the scheduler.
bool LPDDRControllerBase::wck_sync_blocked(const Request& req) {
  int cas = m_cmd_cas >= 0 ? m_cmd_cas : (is_read_cmd(req.command) ? m_cmd_cas_rd : m_cmd_cas_wr);
  return needs_wck_sync(req.command, req.addr_vec) && !check_timing(cas, req.addr_vec);
}

bool LPDDRControllerBase::cas_would_block_deadline() const {
  for (int bank_id = 0; bank_id < static_cast<int>(m_act2_owner_valid.size()); bank_id++) {
    if (!m_act2_owner_valid[bank_id]) {
      continue;
    }
    // CAS reserves the next tick for the forced RD/WR continuation. Reject it
    // if that would consume the last legal ACT2 issue slot for any pending
    // split activation.
    if (m_act2_deadline[bank_id] <= m_clk + m_cas_deadline_guard) {
      return true;
    }
  }
  return false;
}

bool LPDDRControllerBase::would_block_activating(int cmd, const AddrVec_t& addr_vec) const {
  const auto& meta = m_device.m_spec->command_meta[cmd];
  if (!meta.is_closing && !meta.is_refreshing) {
    return false;
  }

  bool blocked = false;
  m_device.for_each_target_bank_while(cmd, addr_vec, [&](int bank_id) {
    if (m_act2_owner_valid[bank_id]) {
      blocked = true;
      return false;
    }
    return true;
  });
  return blocked;
}

bool LPDDRControllerBase::has_pending_act2() const {
  for (bool valid : m_act2_owner_valid) {
    if (valid) {
      return true;
    }
  }
  return false;
}

bool LPDDRControllerBase::is_cas_cmd(int cmd) const {
  return cmd == m_cmd_cas || cmd == m_cmd_cas_rd || cmd == m_cmd_cas_wr;
}

bool LPDDRControllerBase::is_allowed_during_pending_act2(int cmd, const AddrVec_t& addr_vec) const {
  if (!has_pending_act2()) {
    return true;
  }
  if (cmd == m_cmd_act1) {
    return false;
  }
  if (is_cas_cmd(cmd) || is_access_cmd(cmd)) {
    return true;
  }

  const auto& meta = m_device.m_spec->command_meta[cmd];
  if (meta.is_refreshing) {
    return false;
  }
  if (meta.is_closing) {
    return !would_block_activating(cmd, addr_vec);
  }
  return false;
}

bool LPDDRControllerBase::can_start_act1(const Request& req) {
  if (req.command != m_cmd_act1) {
    return true;
  }

  Clk_t earliest_act2 = m_clk + m_device.m_spec->command_cycles[m_cmd_act1];
  return m_device.check_timing(m_cmd_act2, req.addr_vec, earliest_act2);
}

bool LPDDRControllerBase::is_owned_act2_candidate(const Request& req) const {
  if (req.command != m_cmd_act2) {
    return true;
  }

  int flat_bank_id = m_device.get_flat_bank_id(req.addr_vec);
  assert(m_act2_owner_valid[flat_bank_id]);

  // Only the owner request stored in m_activating_buffer may issue ACT2.
  return false;
}

ControllerBase::Candidate LPDDRControllerBase::select_normal_candidate() {
  Candidate cand = pick_best_ready_from(m_active_buffer, [&](const Request& req) {
    return is_allowed_during_pending_act2(req.command, req.addr_vec) && can_start_act1(req) && !wck_sync_blocked(req);
  });
  if (!cand.valid) {
    cand = pick_priority_if([&](const Request& req) {
      return is_owned_act2_candidate(req) && is_allowed_during_pending_act2(req.command, req.addr_vec) &&
             can_start_act1(req) && !wck_sync_blocked(req);
    });
  }
  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if([&](const Request& req) {
      return is_owned_act2_candidate(req) && is_allowed_during_pending_act2(req.command, req.addr_vec) &&
             can_start_act1(req) && !wck_sync_blocked(req);
    });
  }
  return cand;
}

/**
 * Select an active-buffer access whose WCK synchronization can begin now.
 * Normal selection requires the RD/WR to be ready in the current cycle; this
 * lookahead accepts it when CAS is ready now and the RD/WR will be ready after
 * the CAS guard, provided the sequence cannot block a pending ACT2 deadline.
 */
ControllerBase::Candidate LPDDRControllerBase::pick_cas_lookahead_candidate() {
  Candidate cand;
  if (cas_would_block_deadline()) {
    return cand;
  }

  auto it = m_scheduler->get_best_request(m_active_buffer, [&](const Request& req) {
    int cmd = req.command;
    if (!is_access_cmd(cmd) || !needs_wck_sync(cmd, req.addr_vec) ||
        !is_allowed_during_pending_act2(cmd, req.addr_vec)) {
      return false;
    }

    int cas = m_cmd_cas >= 0 ? m_cmd_cas : (is_read_cmd(cmd) ? m_cmd_cas_rd : m_cmd_cas_wr);
    return check_timing(cas, req.addr_vec) &&
           m_device.check_timing(cmd, req.addr_vec, m_clk + m_cas_deadline_guard);
  });
  if (it == m_active_buffer.end()) {
    return cand;
  }

  cand.valid = true;
  cand.it = it;
  cand.buffer = &m_active_buffer;
  return cand;
}

ControllerBase::Candidate LPDDRControllerBase::pick_urgent_act2() {
  Candidate best;
  Clk_t best_deadline = -1;

  for (auto it = m_activating_buffer.begin(); it != m_activating_buffer.end(); it++) {
    int flat_bank_id = m_device.get_flat_bank_id(it->addr_vec);
    assert(m_act2_owner_valid[flat_bank_id]);

    Clk_t deadline = m_act2_deadline[flat_bank_id];
    assert(deadline >= 0);

    it->command = m_cmd_act2;

    bool timing_ok = check_timing(it->command, it->addr_vec);
    assert(deadline > m_clk || timing_ok);
    if (deadline > m_clk || !timing_ok) {
      continue;
    }

    if (!best.valid || deadline < best_deadline || (deadline == best_deadline && it->arrive < best.it->arrive)) {
      best.valid = true;
      best.it = it;
      best.buffer = &m_activating_buffer;
      best_deadline = deadline;
    }
  }

  return best;
}

ControllerBase::Candidate LPDDRControllerBase::pick_deferred_act2() {
  Candidate best;
  Clk_t best_deadline = -1;

  for (auto it = m_activating_buffer.begin(); it != m_activating_buffer.end(); it++) {
    int flat_bank_id = m_device.get_flat_bank_id(it->addr_vec);
    assert(m_act2_owner_valid[flat_bank_id]);

    Clk_t deadline = m_act2_deadline[flat_bank_id];
    assert(deadline >= 0);

    it->command = m_cmd_act2;
    if (!check_timing(it->command, it->addr_vec)) {
      continue;
    }

    if (!best.valid || deadline < best_deadline || (deadline == best_deadline && it->arrive < best.it->arrive)) {
      best.valid = true;
      best.it = it;
      best.buffer = &m_activating_buffer;
      best_deadline = deadline;
    }
  }

  return best;
}

void LPDDRControllerBase::issue_owned_act2(Candidate cand, Act2IssueKind kind) {
  assert(cand.valid);
  assert(cand.buffer == &m_activating_buffer);
  assert(cand.it->command == m_cmd_act2);

  int flat_bank_id = m_device.get_flat_bank_id(cand.it->addr_vec);
  assert(m_act2_owner_valid[flat_bank_id]);
  assert(m_act2_deadline[flat_bank_id] >= 0);
  assert(m_clk <= m_act2_deadline[flat_bank_id]);

  if (!cand.it->is_stat_updated) {
    update_request_stats(cand.it);
  }

  m_device.issue_command(m_cmd_act2, cand.it->addr_vec, m_clk);

  m_rowpolicy->on_issue(*cand.it);
  for (auto* p : m_plugins) {
    p->on_issue(*cand.it);
  }
  promote_from_activating(cand.it, *cand.buffer);

  if (kind == Act2IssueKind::Urgent) {
    s_act2_deadline_forced++;
  } else {
    s_act2_deferred++;
  }
}

bool LPDDRControllerBase::try_issue_cas_sync(Candidate& cand) {
  assert(cand.valid);
  assert(cand.buffer != &m_activating_buffer);

  int cmd = cand.it->command;
  if (!needs_wck_sync(cmd, cand.it->addr_vec)) {
    return false;
  }

  if (cas_would_block_deadline()) {
    Candidate deferred = pick_deferred_act2();
    assert(deferred.valid);
    issue_owned_act2(deferred, Act2IssueKind::Deferred);
    return true;
  }

  int cas = m_cmd_cas >= 0 ? m_cmd_cas : (is_read_cmd(cmd) ? m_cmd_cas_rd : m_cmd_cas_wr);
  if (!check_timing(cas, cand.it->addr_vec)) {
    throw std::runtime_error("LPDDRControllerBase: WCK2CK sync CAS selected while not timing-ready");
  }
  int saved = cand.it->command;
  cand.it->command = cas;

  m_device.issue_command(cas, cand.it->addr_vec, m_clk);
  m_rowpolicy->on_issue(*cand.it);
  for (auto* p : m_plugins) {
    p->on_issue(*cand.it);
  }

  cand.it->command = saved;
  m_cas_issued = true;
  m_cas_req_it = cand.it;
  m_cas_buffer = cand.buffer;
  return true;
}

void LPDDRControllerBase::issue_standard_candidate(Candidate cand) {
  assert(cand.valid);
  assert(cand.buffer != &m_activating_buffer);

  m_rowpolicy->try_upgrade_command(*cand.it);
  int cmd = cand.it->command;
  int flat_bank_id = m_device.get_flat_bank_id(cand.it->addr_vec);

  if (cmd == m_cmd_act1) {
    assert(!m_act2_owner_valid[flat_bank_id]);
  }

  if (is_access_cmd(cmd)) {
    if (m_wck_sync_mode == WCKSyncMode::NeedSync) {
      extend_wck_expiry(cmd, cand.it->addr_vec);
    }
    s_cas_skipped++;
  }

  if (!cand.it->is_stat_updated) {
    update_request_stats(cand.it);
  }

  m_device.issue_command(cmd, cand.it->addr_vec, m_clk);

  m_rowpolicy->on_issue(*cand.it);
  for (auto* p : m_plugins) {
    p->on_issue(*cand.it);
  }

  if (cmd == m_cmd_act1) {
    move_to_activating(cand.it, *cand.buffer);
  } else if (cand.it->command == cand.it->final_command) {
    retire_request(cand.it, *cand.buffer);
  } else if (m_device.m_spec->command_meta[cand.it->command].is_opening) {
    promote_to_active(cand.it, *cand.buffer);
  }
}

void LPDDRControllerBase::move_to_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
  int flat_bank_id = m_device.get_flat_bank_id(req_it->addr_vec);
  assert(!m_act2_owner_valid[flat_bank_id]);

  bool enqueued = m_activating_buffer.enqueue(*req_it);
  assert(enqueued);

  // Erase from write-forwarding set before removing from the source buffer.
  // Without this, the address leaks in the set (the request moves through
  // m_activating_buffer -> m_active_buffer, neither of which triggers the
  // erase guard in promote_to_active/retire_request).
  if (&buffer == &m_write_buffer) {
    m_buffered_write_addrs.erase(req_it->addr);
  }
  buffer.remove(req_it);
  m_act2_owner_valid[flat_bank_id] = true;
  m_act2_deadline[flat_bank_id] = m_clk + m_nAAD;
}

void LPDDRControllerBase::promote_from_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
  int flat_bank_id = m_device.get_flat_bank_id(req_it->addr_vec);
  assert(m_act2_owner_valid[flat_bank_id]);
  assert(m_clk <= m_act2_deadline[flat_bank_id]);

  size_t old_size = m_active_buffer.size();
  promote_to_active(req_it, buffer);
  assert(m_active_buffer.size() == old_size + 1);

  m_act2_owner_valid[flat_bank_id] = false;
  m_act2_deadline[flat_bank_id] = -1;
}

}  // namespace Ramulator
