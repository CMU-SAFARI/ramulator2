#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/refresh.h"

namespace Ramulator {

// Same-Bank refresh manager.
//
// Issues `same-bank-refresh` requests (mapped to REFsb on supported
// DRAM standards, currently DDR5 in this repo) in an order that visits
// every (sub-channel, bank-group) combination for a given bank index
// before advancing to the next bank index:
//
//   bank_index 0:  (rank0, BG0, B0), (rank0, BG1, B0), ... , (rankR-1, BGN-1, B0)
//   bank_index 1:  (rank0, BG0, B1), (rank0, BG1, B1), ... , (rankR-1, BGN-1, B1)
//   ...
//
// This matches the JEDEC DDR5 "same bank refresh" semantic — refresh
// the bank at *the same bank index* across all bank groups (and ranks)
// before moving to the next bank index. By contrast, the `PerBank`
// manager visits all banks within one bank group before moving to the
// next bank group.
//
// Functionally both managers refresh the same total set of banks at
// the same average rate (one full bank set per `nREFI`); they differ
// in temporal grouping, which matters to schedulers that exploit
// bank-group level parallelism or to RowHammer-style mitigations that
// care about same-bank reuse intervals.
//
// Requirements on the DRAM standard:
//   - exposes "same-bank-refresh" in m_requests
//   - exposes a "bank" level in m_levels
//   - exposes a positive nREFI in m_timing_vals
class SameBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, SameBankRefresh, "SameBank", "Same-Bank Refresh scheme.")

  private:
    Clk_t m_clk = 0;
    IDRAM* m_dram = nullptr;
    IDRAMController* m_ctrl = nullptr;

    int m_same_bank_ref_req_id = -1;

    int m_num_levels = 0;
    int m_bank_level_id = -1;

    // size of each "inner" level (between channel exclusive and bank exclusive),
    // in original level order — e.g. for DDR5: [N_rank, N_BG].
    std::vector<int> m_inner_level_sizes;
    int m_num_inner_combinations = 0;   // product of inner sizes
    int m_num_banks_per_bg = 0;         // outer dimension
    int m_total_refresh_targets = 0;    // == inner * outer

    Clk_t m_per_issue_interval = -1;
    Clk_t m_next_refresh_cycle = -1;

    int m_current_outer_idx = 0;   // current bank index (0 .. m_num_banks_per_bg-1)
    int m_current_inner_idx = 0;   // current (rank,BG) flat index (0 .. m_num_inner_combinations-1)

  public:
    void init() override {
      m_ctrl = cast_parent<IDRAMController>();
    };

    void setup(IFrontEnd* /*frontend*/, IMemorySystem* /*memory_system*/) override {
      m_dram = m_ctrl->m_dram;
      m_num_levels = m_dram->m_levels.size();

      try {
        m_same_bank_ref_req_id = m_dram->m_requests("same-bank-refresh");
      } catch (const std::out_of_range&) {
        throw std::runtime_error(
          "SameBank refresh manager requires the DRAM standard to define a "
          "'same-bank-refresh' request type (e.g. DDR5).");
      }

      try {
        m_bank_level_id = m_dram->m_levels("bank");
      } catch (const std::out_of_range&) {
        throw std::runtime_error(
          "SameBank refresh manager requires the DRAM standard to define a 'bank' level.");
      }

      int nrefi = m_dram->m_timing_vals("nREFI");
      if (nrefi <= 0) {
        throw std::runtime_error(
          "SameBank refresh manager requires nREFI > 0 (got " + std::to_string(nrefi) + ").");
      }

      // Inner levels are everything strictly between channel and bank.
      // For DDR5 ({channel, rank, bankgroup, bank, row, column}) that is
      // levels 1..2 i.e. {rank, bankgroup}.
      int num_inner_levels = m_bank_level_id - 1;   // exclude channel (0) and bank itself
      if (num_inner_levels < 0) {
        throw std::runtime_error(
          "SameBank refresh manager: bank level appears at index 0 — unexpected hierarchy.");
      }
      m_inner_level_sizes.resize(num_inner_levels);
      m_num_inner_combinations = 1;
      for (int l = 1; l < m_bank_level_id; l++) {
        int sz = m_dram->m_organization.count[l];
        if (sz <= 0) {
          throw std::runtime_error(
            "SameBank refresh manager: level " + std::to_string(l) +
            " has non-positive size " + std::to_string(sz) + ".");
        }
        m_inner_level_sizes[l - 1] = sz;
        m_num_inner_combinations *= sz;
      }

      m_num_banks_per_bg = m_dram->m_organization.count[m_bank_level_id];
      if (m_num_banks_per_bg <= 0) {
        throw std::runtime_error(
          "SameBank refresh manager: bank level has non-positive size " +
          std::to_string(m_num_banks_per_bg) + ".");
      }

      m_total_refresh_targets = m_num_inner_combinations * m_num_banks_per_bg;

      // Each refresh target gets one REFsb per nREFI on average.
      m_per_issue_interval = nrefi / m_total_refresh_targets;
      if (m_per_issue_interval == 0) {
        m_per_issue_interval = 1;
      }
      m_next_refresh_cycle = m_per_issue_interval;
    };

    void tick() override {
      m_clk++;
      if (m_clk < m_next_refresh_cycle) {
        return;
      }
      m_next_refresh_cycle += m_per_issue_interval;

      std::vector<int> addr_vec(m_num_levels, -1);
      addr_vec[0] = m_ctrl->m_channel_id;

      // Decode the inner index into level coordinates [1 .. bank_level - 1].
      int idx = m_current_inner_idx;
      for (int l = m_bank_level_id - 1; l >= 1; l--) {
        int sz = m_inner_level_sizes[l - 1];
        addr_vec[l] = idx % sz;
        idx /= sz;
      }
      addr_vec[m_bank_level_id] = m_current_outer_idx;

      Request req(addr_vec, m_same_bank_ref_req_id);
      bool is_success = m_ctrl->priority_send(req);
      if (!is_success) {
        throw std::runtime_error("SameBank refresh: failed to send REFsb request.");
      }

      // Advance: inner first (so we sweep all sub-channel × BG for the same
      // bank index before incrementing the bank index — that is the
      // "same-bank refresh" semantic), then wrap to next outer.
      m_current_inner_idx++;
      if (m_current_inner_idx >= m_num_inner_combinations) {
        m_current_inner_idx = 0;
        m_current_outer_idx = (m_current_outer_idx + 1) % m_num_banks_per_bg;
      }
    };
};

}        // namespace Ramulator
