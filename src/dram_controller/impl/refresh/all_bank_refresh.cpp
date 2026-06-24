#include <vector>

#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/refresh.h"

namespace Ramulator {

class AllBankRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, AllBankRefresh, "AllBank", "All-Bank Refresh scheme.")
  private:
    Clk_t m_clk = 0;
    IDRAM* m_dram;
    IDRAMController* m_ctrl;

    int m_dram_org_levels = -1;

    // Number of independently-refreshed sub-channel divisions per controller.
    // For DDR/LPDDR this is the number of ranks; for HBM family / GDDR6 the
    // all-bank refresh command is channel-scoped, so a single REFab per
    // channel is issued.
    int m_num_sub_channels = -1;
    int m_sub_channel_level = -1;  // -1 means REFab is channel-scoped

    int m_nrefi = -1;
    int m_ref_req_id = -1;
    Clk_t m_next_refresh_cycle = -1;

  public:
    void init() override { 
      m_ctrl = cast_parent<IDRAMController>();
    };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = m_ctrl->m_dram;

      m_dram_org_levels = m_dram->m_levels.size();

      // Prefer "rank" when the DRAM exposes one (DDR3/4/5, LPDDR5, ...).
      // HBM family and GDDR6 do not have a rank level and their REFab is
      // defined at the channel scope per JEDEC, so a single REFab per
      // channel covers all banks. Issuing one REFab per pseudochannel on
      // HBM would violate the spec and double the refresh traffic.
      try {
        m_sub_channel_level = m_dram->m_levels("rank");
        m_num_sub_channels  = m_dram->get_level_size("rank");
      } catch (const std::out_of_range&) {
        m_sub_channel_level = -1;
        m_num_sub_channels  = 1;
      }

      m_nrefi = m_dram->m_timing_vals("nREFI");
      m_ref_req_id = m_dram->m_requests("all-bank-refresh");

      m_next_refresh_cycle = m_nrefi;
    };

    void tick() {
      m_clk++;

      if (m_clk == m_next_refresh_cycle) {
        m_next_refresh_cycle += m_nrefi;
        for (int r = 0; r < m_num_sub_channels; r++) {
          std::vector<int> addr_vec(m_dram_org_levels, -1);
          addr_vec[0] = m_ctrl->m_channel_id;
          if (m_sub_channel_level >= 0) {
            addr_vec[m_sub_channel_level] = r;
          }
          Request req(addr_vec, m_ref_req_id);

          bool is_success = m_ctrl->priority_send(req);
          if (!is_success) {
            throw std::runtime_error("Failed to send refresh!");
          }
        }
      }
    };

};

}       // namespace Ramulator
