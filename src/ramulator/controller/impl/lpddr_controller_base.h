#ifndef RAMULATOR_CONTROLLER_IMPL_LPDDR_CONTROLLER_BASE_H
#define RAMULATOR_CONTROLLER_IMPL_LPDDR_CONTROLLER_BASE_H

#include <string>
#include <vector>

#include "ramulator/controller/controller_base.h"

namespace Ramulator {

// Shared LPDDR controller logic for split-activate scheduling and WCK2CK sync.
class LPDDRControllerBase : public ControllerBase {
 public:
  void init() override;
  void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override;
  void tick() override;
  void reset_stats() override;

 protected:
  LPDDRControllerBase(const ConfigNode& config, Implementation* parent)
      : ControllerBase(config, parent) {
  }

  enum class Act2IssueKind {
    Urgent,
    Deferred,
  };

  enum class WCKSyncMode {
    NeedSync,
    AlwaysOn,
  };

  // Split-activate state
  ReqBuffer m_activating_buffer;
  int m_cmd_act1 = -1;
  int m_cmd_act2 = -1;
  std::vector<bool> m_act2_owner_valid;
  std::vector<Clk_t> m_act2_deadline;
  int m_nAAD = 0;

  // WCK management
  Clk_t m_wck_expiry = 0;
  int m_cmd_cas = -1;
  int m_cmd_cas_rd = -1;
  int m_cmd_cas_wr = -1;
  int m_cmd_rd = -1;
  int m_cmd_wr = -1;
  int m_cmd_rda = -1;
  int m_cmd_wra = -1;
  int m_cmd_rd_l = -1;
  int m_cmd_wr_l = -1;
  int m_cmd_rda_l = -1;
  int m_cmd_wra_l = -1;
  int m_read_latency = 0;
  int m_write_latency = 0;
  int m_burst_cycles = 0;
  int m_burst_cycles_long = 0;
  int m_nWCKPST = 0;
  int m_cas_deadline_guard = 1;
  WCKSyncMode m_wck_sync_mode = WCKSyncMode::NeedSync;

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
  int burst_cycles_for_cmd(int cmd) const;
  WCKSyncMode parse_wck_sync_mode(const std::string& mode) const;
  bool needs_wck_sync(int cmd) const;
  void extend_wck_expiry(int cmd);

  bool cas_would_block_deadline() const;
  bool would_block_activating(int cmd, const AddrVec_t& addr_vec) const;
  bool has_pending_act2() const;
  bool is_cas_cmd(int cmd) const;
  bool is_allowed_during_pending_act2(int cmd, const AddrVec_t& addr_vec) const;
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

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_IMPL_LPDDR_CONTROLLER_BASE_H
