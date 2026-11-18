#include <memory>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/refresh_engines.h"

namespace Ramulator {

class SMDFixedRateRefresh : public ISMDMechanism, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(ISMDMechanism, SMDFixedRateRefresh, "FixedRateRefresh")

public:
    void init() override {
        RAMULATOR_PARSE_PARAM(m_period, int64_t, "refresh_period").default_val(102400000);
        RAMULATOR_PARSE_PARAM(m_num_machines, int, "num_ref_machines").default_val(4);
        RAMULATOR_PARSE_PARAM(m_granularity, int, "row_refresh_granularity").default_val(8);
        RAMULATOR_PARSE_PARAM(m_latency, int, "single_ref_latency").default_val(80);
        RAMULATOR_PARSE_PARAM(m_pending_limit, int, "pending_ref_limit").default_val(9);
        RAMULATOR_PARSE_PARAM(m_max_row_open_intervals, int, "max_row_open_intervals").default_val(8);
        RAMULATOR_PARSE_PARAM(m_timeout_ratio, float, "timeout_to_ref_interval_ratio").default_val(0.5f);
        RAMULATOR_PARSE_PARAM(m_worst_case, bool, "worst_case_ref_distribution").default_val(false);
        m_params = {m_period, m_num_machines, m_granularity, m_latency, m_pending_limit, m_max_row_open_intervals, m_timeout_ratio, m_worst_case};
    }

    bool needs_chip_simulation() const override {
        return m_params.worst_case_distribution;
    }

    void attach(SMDContext& ctx) override {
        m_engines.build(ctx, m_params, [](int, int, const SMD::EngineConfig&) {
            return std::make_unique<SMD::FixedRatePolicy>();
        });
    }

    void tick() override { m_engines.tick(); }
    int64_t row_open_limit() const override { return m_engines.row_open_limit(); }
    void collect_smd_stats(SMDStatMap& out) const override { m_engines.collect(out); }
    void reset_smd_stats() override { m_engines.reset(); }

private:
    int64_t m_period = 0;
    int m_num_machines = 0;
    int m_granularity = 0;
    int m_latency = 0;
    int m_pending_limit = 0;
    int m_max_row_open_intervals = 0;
    float m_timeout_ratio = 0;
    bool m_worst_case = false;
    SMD::RefreshParams m_params;
    SMD::RefreshEngines m_engines;
};

}  // namespace Ramulator