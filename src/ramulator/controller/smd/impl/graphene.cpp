#include <cmath>
#include <cstdint>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/rowhammer.h"

namespace Ramulator {

class SMDGraphene : public ISMDMechanism, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(ISMDMechanism, SMDGraphene, "GrapheneProtection")

public:
    void init() override {
        RAMULATOR_PARSE_PARAM(m_mac, int, "mac").default_val(8192);
        RAMULATOR_PARSE_PARAM(m_refresh_window, int64_t, "refresh_window").default_val(102400000);
        RAMULATOR_PARSE_PARAM(m_blast_radius, int, "blast_radius").default_val(1);
        RAMULATOR_PARSE_PARAM(m_queue_size, int, "neighbor_ref_queue_size").default_val(1024);
        RAMULATOR_PARSE_PARAM(m_latency, int, "single_ref_latency").default_val(80);
        RAMULATOR_PARSE_PARAM(m_num_counters_param, int, "num_counters").default_val(0);
        RAMULATOR_PARSE_PARAM(m_legacy_eviction, bool, "legacy_eviction").default_val(false);
        m_cfg = {m_latency, m_blast_radius, m_queue_size};
    }

    void attach(SMDContext& ctx) override {
        m_cfg.latency = ctx.resolve_latency(m_cfg.latency);

        const float nRFC = ctx.spec->get_timing_value("nRFC");
        const float nREFI = ctx.spec->get_timing_value("nREFI");
        const uint32_t nRC = ctx.spec->get_timing_value("nRC");
        const auto acts_per_window = static_cast<uint32_t>(
            std::floor((static_cast<uint32_t>(m_refresh_window) * (1 - nRFC / nREFI)) / nRC)
        );
        const uint32_t act_threshold = m_mac / 2;
        uint32_t num_counters = act_threshold > 0 ? acts_per_window / act_threshold : 0;
        if (m_num_counters_param > 0) {
            num_counters = m_num_counters_param;
        }

        m_chips.build(ctx, m_cfg, [&](int,int) {
            return SMD::GrapheneDetector(ctx.banks_per_rank, ctx.rows_per_sa, num_counters, act_threshold, m_refresh_window, m_legacy_eviction);
        });
    }

    void tick() override { m_chips.tick(); }
    void on_activation(int bank, int sa, int row) override { m_chips.on_activation(bank, sa, row); }
    void collect_smd_stats(SMDStatMap& out) const override { m_chips.collect(out); }
    void reset_smd_stats() override { m_chips.reset(); }
    
private:
    int m_latency = 0;
    int m_blast_radius = 0;
    int m_queue_size = 0;
    int m_mac = 0;
    int m_num_counters_param = 0;
    bool m_legacy_eviction = false;
    int64_t m_refresh_window = 0;
    SMD::NeighborRefreshConfig m_cfg;
    SMD::RowHammerChips<SMD::GrapheneDetector> m_chips;
};

}  // namespace Ramulator