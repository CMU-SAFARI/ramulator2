#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/refresh_engines.h"
#include "ramulator/controller/smd/variable_refresh.h"

namespace Ramulator {

class SMDVariableRefresh : public ISMDMechanism, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(ISMDMechanism, SMDVariableRefresh, "VariableRefresh")
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
        RAMULATOR_PARSE_PARAM(m_distribution, std::string, "distribution").default_val("discrete");
        RAMULATOR_PARSE_PARAM(m_weak_row_percentage, float, "weak_row_percentage").default_val(0.05f);
        RAMULATOR_PARSE_PARAM(m_bloom_size, int, "bloom_filter_size").default_val(8192);
        RAMULATOR_PARSE_PARAM(m_bloom_hashes, int, "bloom_filter_hashes").default_val(6);
        m_params = {m_period, m_num_machines, m_granularity, m_latency, m_pending_limit, m_max_row_open_intervals, m_timeout_ratio, m_worst_case};
    }

    bool needs_chip_simulation() const override { return true; }
    
    void attach(SMDContext& ctx) override {
        SMD::VariableRefreshConfig vc;
        vc.distribution = m_distribution;
        vc.weak_row_percentage = m_weak_row_percentage;
        vc.bloom_size = m_bloom_size;
        vc.bloom_hashes = m_bloom_hashes;
        vc.refresh_period_ms = std::ceil(m_params.period * (ctx.tCK_ps / 1000.0) / 1e6);

        m_engines.build(ctx, m_params, [&](int rank, int chip, const SMD::EngineConfig& cfg) {
            vc.seed = rank * ctx.chips_per_rank + chip;
            auto policy = std::make_unique<SMD::VariableRefreshPolicy>(vc, cfg, rank);
            m_policies.push_back(policy.get());
            return policy;
        });
    }

    void tick() override {
        m_engines.tick();
    }

    int64_t row_open_limit() const override {
        return m_engines.row_open_limit();
    }

    void collect_smd_stats(SMDStatMap& out) const override {
        m_engines.collect(out);
        for (const auto* policy : m_policies) {
            out["bloom_positives"] += policy->filter().positives();
            out["bloom_false_positives"] += policy->filter().false_positives();
        }
    }

    void reset_smd_stats() override {
        m_engines.reset();
        for (auto* policy : m_policies) {
            policy->reset_stats();
        }
    }

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
    std::string m_distribution;
    float m_weak_row_percentage = 0;
    int m_bloom_size = 0;
    int m_bloom_hashes = 0;

    SMD::RefreshEngines m_engines;
    std::vector<SMD::VariableRefreshPolicy*> m_policies;
};

}  // namespace Ramulator