#ifndef RAMULATOR_CONTROLLER_SMD_REFRESH_ENGINES_H
#define RAMULATOR_CONTROLLER_SMD_REFRESH_ENGINES_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/maintenance.h"

namespace Ramulator::SMD {

struct RefreshParams {
    int64_t period = 0;
    int num_machines = 4;       // maintenance machines per rank
    int granularity = 8;        // rows per maintenance operation
    int latency = 80;           // cycles to refresh one row
    int pending_limit = 9;      // maintenance operations a bank may owe
    int max_row_open_intervals = 8;
    float timeout_to_interval_ratio = 0.5f;
    bool worst_case_distribution = false;
};

class RefreshEngines {
public:
    using PolicyFactory = std::function<std::unique_ptr<RefreshPolicy>(int rank, int chip, const EngineConfig&)>;

    void build(const SMDContext& ctx, const RefreshParams& p, const PolicyFactory& make_policy) {
        m_num_chips = ctx.num_chips;
        m_num_ranks = ctx.num_ranks;

        EngineConfig cfg;
        cfg.num_ranks = ctx.num_ranks;
        cfg.banks_per_rank = ctx.banks_per_rank;
        cfg.num_sas = ctx.num_sas;
        cfg.rows_per_sa = ctx.rows_per_sa;
        cfg.refresh_period = p.period;
        cfg.latency = p.latency;
        cfg.auto_latency = ctx.auto_latency();
        cfg.granularity = p.granularity;
        cfg.num_machines = p.num_machines;
        cfg.pending_limit = p.pending_limit;
        cfg.worst_case_distribution = p.worst_case_distribution;

        for (int chip = 0; chip < ctx.num_chips; chip++) {
            for (int rank = 0; rank < ctx.num_ranks; rank++) {
                m_policies.push_back(make_policy(rank, chip, cfg));
                m_engines.push_back(std::make_unique<RankEngine>(cfg, rank, chip, *ctx.locks[chip], ctx.is_sa_active, *m_policies.back()));
            }
        }

        const int64_t interval = p.period / (static_cast<int64_t>(ctx.rows_per_sa) * ctx.num_sas) * p.granularity;
        m_row_open_limit = static_cast<int64_t>(std::floor(interval * p.timeout_to_interval_ratio)) * p.max_row_open_intervals;
    }

    void tick() {
        for (auto& engine : m_engines) {
            engine->tick();
        }
    }

    int64_t row_open_limit() const { return m_row_open_limit; }

    void collect(SMDStatMap& out) const {
        uint64_t ops = 0, rows = 0, pending = 0;
        
        for (size_t i = 0; i< m_engines.size(); i++) {
            if (static_cast<int>(i) < m_num_ranks) {
                ops += m_engines[i]->num_maintenance_ops();
                rows += m_engines[i]->num_rows_refreshed();
            }
            pending = std::max<uint64_t>(pending, m_engines[i]->max_pending());
        }
        out["maintenance_ops"] += ops - m_ops_base;
        out["rows_refreshed"] += rows - m_rows_base;
        out["max_pending_maintenance"] = std::max(out["max_pending_maintenance"], pending);
    }

    void reset() {
        m_ops_base = m_rows_base = 0;
        for (int i =0; i < m_num_ranks && i < static_cast<int>(m_engines.size()); i++) {
            m_ops_base += m_engines[i]->num_maintenance_ops();
            m_rows_base += m_engines[i]->num_rows_refreshed();
        }
    }

private:
    int m_num_chips = 1;
    int m_num_ranks = 1;
    std::vector<std::unique_ptr<RefreshPolicy>> m_policies;
    std::vector<std::unique_ptr<RankEngine>> m_engines;
    int64_t m_row_open_limit = 0;
    uint64_t m_ops_base = 0;
    uint64_t m_rows_base = 0;
};

}  // namespace Ramulator::SMD

#endif  // RAMULATOR_CONTROLLER_SMD_REFRESH_ENGINES_H