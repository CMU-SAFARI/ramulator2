#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/rowhammer.h"

namespace Ramulator {

class SMDPARA : public ISMDMechanism, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(ISMDMechanism, SMDPARA, "PARAProtection")
public:
    void init() override {
        RAMULATOR_PARSE_PARAM(m_probability_pct, float, "neighbor_refresh_pct").default_val(1.0f);
        RAMULATOR_PARSE_PARAM(m_blast_radius, int, "blast_radius").default_val(1);
        RAMULATOR_PARSE_PARAM(m_queue_size, int, "neighbor_ref_queue_size").default_val(1024);
        RAMULATOR_PARSE_PARAM(m_latency, int, "single_ref_latency").default_val(80);
        RAMULATOR_PARSE_PARAM(m_use_table, bool, "marked_rows_table").default_val(true);
        m_cfg = {m_latency, m_blast_radius, m_queue_size};
    }

    bool needs_chip_simulation() const override { return true; }

    void attach(SMDContext& ctx) override {
        m_cfg.latency = ctx.resolve_latency(m_cfg.latency);
        auto make_detector = [&](int rank, int chip) {
            return SMD::ParaDetector(m_probability_pct, rank * ctx.chips_per_rank + chip);
        };
        if (m_use_table) {
            m_table_chips.build(ctx, m_cfg, make_detector);
        } else {
            m_fifo_chips.build(ctx, m_cfg, make_detector);
        }
    }

    void tick() override {
        m_use_table ? m_table_chips.tick() : m_fifo_chips.tick();
    }

    void on_activation(int bank, int sa, int row) override {
        if (m_use_table) {
            m_table_chips.on_activation(bank, sa, row);
        } else {
            m_fifo_chips.on_activation(bank, sa, row);
        }
    }

    void collect_smd_stats(SMDStatMap& out) const override {
        m_use_table ? m_table_chips.collect(out) : m_fifo_chips.collect(out);
    }

    void reset_smd_stats() override {
        m_use_table ? m_table_chips.reset() : m_fifo_chips.reset();
    }

private:
    int m_latency = 0;
    int m_blast_radius = 0;
    int m_queue_size = 0;
    float m_probability_pct = 0;
    SMD::NeighborRefreshConfig m_cfg;
    bool m_use_table = true;
    SMD::RowHammerChips<SMD::ParaDetector, SMD::MarkedRowsRefresher> m_table_chips;
    SMD::RowHammerChips<SMD::ParaDetector> m_fifo_chips;
};

}  // namespace Ramulator