#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/refresh_engines.h"

namespace Ramulator {

class ScrubPolicy : public SMD::FixedRatePolicy {
public:
    ScrubPolicy(bool lock_bank, int start_sa) : m_lock_bank(lock_bank), m_start_sa(start_sa) {}

    bool blocked_by_open_row(const SMD::MaintenanceCounter& mc, const std::function<bool(int, int)>& sa_active,
                             const std::function<bool(int)>& bank_open) const override {
        return m_lock_bank ? bank_open(mc.bank) : sa_active(mc.bank, mc.sa_counter);
    }

    bool locks_entire_bank() const override {
        return m_lock_bank;
    }

    int initial_sa(int num_sas) const override {
        return m_start_sa % num_sas;
    }

private:
    bool m_lock_bank;
    int m_start_sa;
};

class SMDECCScrubbing : public ISMDMechanism, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(ISMDMechanism, SMDECCScrubbing, "ECCScrubbing")

public:
    void init() override {
        RAMULATOR_PARSE_PARAM(m_period, int64_t, "scrubbing_period").default_val(480000000000);
        RAMULATOR_PARSE_PARAM(m_lock_region, std::string, "scrubbing_lock_region").default_val("bank");
        RAMULATOR_PARSE_PARAM(m_num_machines, int, "num_scrubbing_machines").default_val(1);
        RAMULATOR_PARSE_PARAM(m_granularity, int, "scrubbing_granularity").default_val(1);
        RAMULATOR_PARSE_PARAM(m_latency, int, "single_scrubbing_latency").default_val(552);
        RAMULATOR_PARSE_PARAM(m_pending_limit, int, "pending_scrub_limit").default_val(9);
        RAMULATOR_PARSE_PARAM(m_max_row_open_intervals, int, "max_row_open_intervals").default_val(8);
        RAMULATOR_PARSE_PARAM(m_timeout_ratio, float, "timeout_to_scrubbing_interval_ratio").default_val(0.5f);
        
        if (m_lock_region != "bank" && m_lock_region != "region") {
            throw std::runtime_error("ECCScrubbing: scrubbing_lock_region must be 'bank' or 'region', got '" + m_lock_region + "'");
        }
        m_params.period = m_period;
        m_params.num_machines = m_num_machines;
        m_params.granularity = m_granularity;
        m_params.latency = m_latency;
        m_params.pending_limit = m_pending_limit;
        m_params.max_row_open_intervals = m_max_row_open_intervals;
        m_params.timeout_to_interval_ratio = m_timeout_ratio;
        m_params.stat_ops = "scrub_ops";
        m_params.stat_rows = "rows_scrubbed";
        m_params.stat_pending = "max_pending_scrubs";
    }

    void attach(SMDContext& ctx) override {
        const bool lock_bank = m_lock_region == "bank";
        const int start_sa = ctx.num_sas / 2;
        m_engines.build(ctx, m_params, [=](int, int, const SMD::EngineConfig&) {
            return std::make_unique<ScrubPolicy>(lock_bank, start_sa);
        });
    }

    void tick() override { m_engines.tick(); }
    int64_t row_open_limit() const override { return m_engines.row_open_limit(); }
    void collect_smd_stats(SMDStatMap& out) const override { m_engines.collect(out); }
    void reset_smd_stats() override { m_engines.reset(); }

private:
    int64_t m_period = 0;
    std::string m_lock_region;
    int m_num_machines = 0;
    int m_granularity = 0;
    int m_latency = 0;
    int m_pending_limit = 0;
    int m_max_row_open_intervals = 0;
    float m_timeout_ratio = 0;
    SMD::RefreshParams m_params;
    SMD::RefreshEngines m_engines;
};

} // namespace Ramulator