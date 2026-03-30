#include <fmt/format.h>

#include <cassert>

#include "ramulator/base/base.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

// LPDDR5 controller with split-activate scheduling and WCK2CK synchronization.
//
// In addition to the standard single-issue controller flow, LPDDR5 needs two
// protocol-specific behaviors:
//   1. ACT1/ACT2 split activation with a controller-enforced ACT2 deadline.
//   2. Optional CAS WCK2CK synchronization before RD/WR when WCK is idle.
class LPDDR5Controller : public ControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, LPDDR5Controller, ControllerBase, "LPDDR5")

 public:
  void init() override;
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override;
  void tick() override;

 private:
  enum class Act2IssueKind {
    Urgent,
    Deferred,
  };

  // LPDDR5 split-activate state
  ReqBuffer m_activating_buffer;
  int m_cmd_act1 = -1;
  int m_cmd_act2 = -1;
  std::vector<bool> m_act2_owner_valid;
  std::vector<Clk_t> m_act2_deadline;
  int m_nAAD = 0;

  // WCK management
  Clk_t m_wck_expiry = 0;
  int m_cmd_cas_rd = -1;
  int m_cmd_cas_wr = -1;
  int m_cmd_rd = -1;
  int m_cmd_wr = -1;
  int m_cmd_rda = -1;
  int m_cmd_wra = -1;
  int m_nCL = 0;
  int m_nCWL = 0;
  int m_nBL = 0;
  int m_nWCKPST = 0;

  // Pending access after CAS
  bool m_cas_issued = false;
  ReqBuffer::iterator m_cas_req_it;
  ReqBuffer* m_cas_buffer = nullptr;

  // Extra stats
  size_t s_cas_issued = 0;
  size_t s_cas_skipped = 0;
  size_t s_act2_deadline_forced = 0;
  size_t s_act2_deferred = 0;

  bool is_access_cmd(int cmd) const;
  bool is_read_cmd(int cmd) const;
  void extend_wck_expiry(int cmd);

  bool cas_would_block_deadline() const;
  bool would_block_activating(int cmd, const AddrVec_t& addr_vec) const;
  bool is_owned_act2_candidate(const Request& req) const;

  Candidate select_normal_candidate();
  Candidate pick_urgent_act2();
  Candidate pick_deferred_act2();

  void issue_owned_act2(Candidate cand, Act2IssueKind kind);
  bool try_issue_cas_sync(Candidate& cand);
  void issue_standard_candidate(Candidate cand);
  void move_to_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer);
  void promote_from_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer);
};

void LPDDR5Controller::init() {
  init_base();

  const auto& spec = *m_device.m_spec;
  m_cmd_act1 = spec.get_command_id("ACT1");
  m_cmd_act2 = spec.get_command_id("ACT2");
  m_cmd_cas_rd = spec.get_command_id("CAS_RD");
  m_cmd_cas_wr = spec.get_command_id("CAS_WR");
  m_cmd_rd = spec.get_command_id("RD");
  m_cmd_wr = spec.get_command_id("WR");
  m_cmd_rda = spec.get_command_id("RDA");
  m_cmd_wra = spec.get_command_id("WRA");

  m_nAAD = spec.get_timing_value("nAAD");
  m_nCL = spec.get_timing_value("nCL");
  m_nCWL = spec.get_timing_value("nCWL");
  m_nBL = spec.get_timing_value("nBL");
  m_nWCKPST = spec.get_timing_value("nWCKPST");

  m_activating_buffer.max_size = m_device.m_bank_nodes.size();
  m_act2_owner_valid.assign(m_device.m_bank_nodes.size(), false);
  m_act2_deadline.assign(m_device.m_bank_nodes.size(), -1);
}

void LPDDR5Controller::setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
  setup_base(frontend, memory_system);

  m_stats.add("cas_issued", s_cas_issued);
  m_stats.add("cas_skipped", s_cas_skipped);
  m_stats.add("act2_deadline_forced", s_act2_deadline_forced);
  m_stats.add("act2_deferred", s_act2_deferred);
}

void LPDDR5Controller::tick() {
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
      extend_wck_expiry(m_cas_req_it->command);
      s_cas_issued++;

      m_device.issue_command(m_cas_req_it->command, m_cas_req_it->addr_vec, m_clk);

      if (!m_cas_req_it->is_stat_updated) {
        update_request_stats(m_cas_req_it);
      }

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
    Candidate deferred = pick_deferred_act2();
    if (deferred.valid) {
      issue_owned_act2(deferred, Act2IssueKind::Deferred);
    }
    m_rowpolicy->post_schedule();
    for (auto* p : m_plugins) {
      p->post_schedule();
    }
    return;
  }

  if (cand.buffer == &m_activating_buffer) {
    issue_owned_act2(cand, Act2IssueKind::Urgent);
  } else if (!try_issue_cas_sync(cand)) {
    issue_standard_candidate(cand);
  }

  m_rowpolicy->post_schedule();
  for (auto* p : m_plugins) {
    p->post_schedule();
  }
}

bool LPDDR5Controller::is_access_cmd(int cmd) const {
  return cmd == m_cmd_rd || cmd == m_cmd_wr || cmd == m_cmd_rda || cmd == m_cmd_wra;
}

bool LPDDR5Controller::is_read_cmd(int cmd) const {
  return cmd == m_cmd_rd || cmd == m_cmd_rda;
}

void LPDDR5Controller::extend_wck_expiry(int cmd) {
  int lat = is_read_cmd(cmd) ? m_nCL : m_nCWL;
  Clk_t exp = m_clk + lat + m_nBL + m_nWCKPST;
  if (exp > m_wck_expiry) {
    m_wck_expiry = exp;
  }
}

bool LPDDR5Controller::cas_would_block_deadline() const {
  for (int bank_id = 0; bank_id < static_cast<int>(m_act2_owner_valid.size()); bank_id++) {
    if (!m_act2_owner_valid[bank_id]) {
      continue;
    }
    // CAS reserves the next tick for the forced RD/WR continuation. Reject it
    // if that would consume the last legal ACT2 issue slot for any pending
    // split activation.
    if (m_act2_deadline[bank_id] <= m_clk + 1) {
      return true;
    }
  }
  return false;
}

bool LPDDR5Controller::would_block_activating(int cmd, const AddrVec_t& addr_vec) const {
  const auto& meta = m_device.m_spec->command_meta[cmd];
  if (!meta.is_closing && !meta.is_refreshing) return false;

  bool blocked = false;
  m_device.for_each_target_bank_while(cmd, addr_vec, [&](int bank_id) {
    if (m_act2_owner_valid[bank_id]) { blocked = true; return false; }
    return true;
  });
  return blocked;
}

bool LPDDR5Controller::is_owned_act2_candidate(const Request& req) const {
  if (req.command != m_cmd_act2) {
    return true;
  }

  int flat_bank_id = m_device.get_flat_bank_id(req.addr_vec);
  assert(m_act2_owner_valid[flat_bank_id]);

  // Only the owner request stored in m_activating_buffer may issue ACT2.
  return false;
}

ControllerBase::Candidate LPDDR5Controller::select_normal_candidate() {
  Candidate cand = pick_best_ready_from(m_active_buffer, {});
  if (!cand.valid) {
    cand = pick_priority_if([&](const Request& req) {
      return is_owned_act2_candidate(req) &&
             !would_block_activating(req.command, req.addr_vec);
    });
  }
  if (!cand.valid && m_priority_buffer.size() == 0) {
    cand = pick_rw_if([&](const Request& req) {
      return is_owned_act2_candidate(req) &&
             !would_block_activating(req.command, req.addr_vec);
    });
  }
  return cand;
}

ControllerBase::Candidate LPDDR5Controller::pick_urgent_act2() {
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

ControllerBase::Candidate LPDDR5Controller::pick_deferred_act2() {
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

void LPDDR5Controller::issue_owned_act2(Candidate cand, Act2IssueKind kind) {
  assert(cand.valid);
  assert(cand.buffer == &m_activating_buffer);
  assert(cand.it->command == m_cmd_act2);

  int flat_bank_id = m_device.get_flat_bank_id(cand.it->addr_vec);
  assert(m_act2_owner_valid[flat_bank_id]);
  assert(m_act2_deadline[flat_bank_id] >= 0);
  assert(m_clk <= m_act2_deadline[flat_bank_id]);

  m_device.issue_command(m_cmd_act2, cand.it->addr_vec, m_clk);

  if (!cand.it->is_stat_updated) {
    update_request_stats(cand.it);
  }

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

bool LPDDR5Controller::try_issue_cas_sync(Candidate& cand) {
  assert(cand.valid);
  assert(cand.buffer != &m_activating_buffer);

  int cmd = cand.it->command;
  if (!is_access_cmd(cmd) || m_clk < m_wck_expiry) {
    return false;
  }

  if (cas_would_block_deadline()) {
    Candidate deferred = pick_deferred_act2();
    assert(deferred.valid);
    issue_owned_act2(deferred, Act2IssueKind::Deferred);
    return true;
  }

  int cas = is_read_cmd(cmd) ? m_cmd_cas_rd : m_cmd_cas_wr;
  if (check_timing(cas, cand.it->addr_vec)) {
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
  }

  return true;
}

void LPDDR5Controller::issue_standard_candidate(Candidate cand) {
  assert(cand.valid);
  assert(cand.buffer != &m_activating_buffer);

  m_rowpolicy->try_upgrade_command(*cand.it);
  int cmd = cand.it->command;
  int flat_bank_id = m_device.get_flat_bank_id(cand.it->addr_vec);

  if (cmd == m_cmd_act1) {
    assert(!m_act2_owner_valid[flat_bank_id]);
  }

  if (is_access_cmd(cmd)) {
    extend_wck_expiry(cmd);
    s_cas_skipped++;
  }

  m_device.issue_command(cmd, cand.it->addr_vec, m_clk);

  if (!cand.it->is_stat_updated) {
    update_request_stats(cand.it);
  }

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

void LPDDR5Controller::move_to_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
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

void LPDDR5Controller::promote_from_activating(ReqBuffer::iterator& req_it, ReqBuffer& buffer) {
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
