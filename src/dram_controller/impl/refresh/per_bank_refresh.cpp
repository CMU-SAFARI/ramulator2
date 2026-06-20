#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/refresh.h"

namespace Ramulator {

// Per-Bank refresh manager.
//
// Issues `per-bank-refresh` requests (mapped to REFsb on supported DRAM
// standards: HBM, HBM2, HBM3, LPDDR5) in a round-robin fashion across
// every bank within the controller's channel. The average issue
// interval is nREFI / N_banks_per_channel, so each individual bank is
// refreshed on the same average period as in the all-bank scheme (every
// nREFI), but the refresh work is serialized across banks instead of
// stalling all banks at once.
//
// This unlocks the REFsb command pathway that is already defined in the
// HBM*/LPDDR5 DRAM implementations but has no producer in the existing
// refresh manager set (`AllBank` only).
//
// Requirements on the DRAM standard:
//   - exposes "per-bank-refresh" in m_requests
//   - exposes a "bank" level in m_levels
//   - exposes a positive nREFI in m_timing_vals
//
// Note on nREFISB: the JEDEC tREFISB parameter (minimum interval
// between REFsb commands to the same bank) is not consulted directly
// for pacing here; round-robin scheduling across banks naturally
// guarantees the same-bank constraint is satisfied as long as
// nREFI / N_banks <= nREFISB, which holds for all current presets.
class PerBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, PerBankRefresh, "PerBank", "Per-Bank Refresh scheme.")

  private:
    Clk_t m_clk = 0;
    IDRAM* m_dram = nullptr;
    IDRAMController* m_ctrl = nullptr;

    int m_per_bank_ref_req_id = -1;

    int m_num_levels = 0;
    int m_bank_level_id = -1;
    // size of each level between channel (exclusive) and bank (inclusive)
    std::vector<int> m_sub_channel_level_sizes;
    int m_num_banks_per_channel = 0;

    Clk_t m_per_bank_interval = -1;
    Clk_t m_next_refresh_cycle = -1;

    int m_current_bank_idx = 0;

  public:
    void init() override {
      m_ctrl = cast_parent<IDRAMController>();
    };

    void setup(IFrontEnd* /*frontend*/, IMemorySystem* /*memory_system*/) override {
      m_dram = m_ctrl->m_dram;
      m_num_levels = m_dram->m_levels.size();

      try {
        m_per_bank_ref_req_id = m_dram->m_requests("per-bank-refresh");
      } catch (const std::out_of_range&) {
        throw std::runtime_error(
          "PerBank refresh manager requires the DRAM standard to define a "
          "'per-bank-refresh' request type (e.g. HBM/HBM2/HBM3/LPDDR5).");
      }

      try {
        m_bank_level_id = m_dram->m_levels("bank");
      } catch (const std::out_of_range&) {
        throw std::runtime_error(
          "PerBank refresh manager requires the DRAM standard to define a 'bank' level.");
      }

      int nrefi = m_dram->m_timing_vals("nREFI");
      if (nrefi <= 0) {
        throw std::runtime_error(
          "PerBank refresh manager requires nREFI > 0 (got " + std::to_string(nrefi) + ").");
      }

      // Enumerate all levels between channel (exclusive) and bank (inclusive)
      // to size the round-robin space and to decode a flat bank index into
      // multi-level address coordinates later.
      m_sub_channel_level_sizes.resize(m_bank_level_id);
      m_num_banks_per_channel = 1;
      for (int level = 1; level <= m_bank_level_id; level++) {
        int sz = m_dram->m_organization.count[level];
        if (sz <= 0) {
          throw std::runtime_error(
            "PerBank refresh manager: level " + std::to_string(level) +
            " has non-positive size " + std::to_string(sz) + ".");
        }
        m_sub_channel_level_sizes[level - 1] = sz;
        m_num_banks_per_channel *= sz;
      }

      // Round-robin pacing: each bank gets one REFsb per nREFI on average.
      m_per_bank_interval = nrefi / m_num_banks_per_channel;
      if (m_per_bank_interval == 0) {
        m_per_bank_interval = 1;
      }
      m_next_refresh_cycle = m_per_bank_interval;
    };

    void tick() override {
      m_clk++;
      if (m_clk < m_next_refresh_cycle) {
        return;
      }
      m_next_refresh_cycle += m_per_bank_interval;

      std::vector<int> addr_vec(m_num_levels, -1);
      addr_vec[0] = m_ctrl->m_channel_id;

      // Decode the round-robin bank index into multi-level coordinates,
      // walking from the bank level up toward the channel.
      int idx = m_current_bank_idx;
      for (int level = m_bank_level_id; level >= 1; level--) {
        int sz = m_sub_channel_level_sizes[level - 1];
        addr_vec[level] = idx % sz;
        idx /= sz;
      }

      Request req(addr_vec, m_per_bank_ref_req_id);
      bool is_success = m_ctrl->priority_send(req);
      if (!is_success) {
        throw std::runtime_error("PerBank refresh: failed to send REFsb request.");
      }

      m_current_bank_idx = (m_current_bank_idx + 1) % m_num_banks_per_channel;
    };
};

}        // namespace Ramulator
