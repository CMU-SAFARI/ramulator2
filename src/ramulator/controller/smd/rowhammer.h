#ifndef RAMULATOR_CONTROLLER_SMD_ROWHAMMER_H
#define RAMULATOR_CONTROLLER_SMD_ROWHAMMER_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ramulator/controller/smd/maintenance.h"

namespace Ramulator::SMD {
/*
    RowHammer protection inside the DRAM chip (SMD-DRP / SMD-PRP). The chip watches the activations it serves,
    and when a row looks like a agressor it refreshes the agressor's neighbors on its own: it locks the aggressors'
    subarray like any other maintenance operation, so an ACT that arrives meanwhile is NACKed.
*/

// Where an agressor row sits.
struct RowAddr {
    int bank = 0;
    int sa = 0;
    int row = 0;

    bool operator==(const RowAddr& o) const {
        return bank == o.bank && sa == o.sa && row == o.row;
    }
};

class GrapheneCounterTable {
public:
    GrapheneCounterTable(uint32_t num_counters, uint32_t act_threshold)
        : m_num_counters(num_counters), m_act_threshold(act_threshold) {}
    
    bool increment(uint32_t row_id) {
        auto it = m_counters.find(row_id);
        if (it != m_counters.end()) {
            return ++it->second % m_act_threshold == 0;
        }

        if (m_counters.size() < m_num_counters) {
            m_counters[row_id] = 1;
            return false;
        }

        auto min_counter = smallest_entry();
        if (min_counter->second != m_spillover) {
            m_spillover++;
            return false;
        }

        // The spillover counter caught up with the smalest entry: that entry makes room for the new row
        m_counters.erase(min_counter);
        m_counters[row_id] = m_spillover + 1;
        return m_counters[row_id] % m_act_threshold == 0;
    }

    void reset() {
        m_spillover = 0;
        m_counters.clear();
    }

private:
    std::map<uint32_t, uint32_t>::iterator smallest_entry() {
        return std::min_element(m_counters.begin(), m_counters.end());
    }

    uint32_t m_spillover = 0;
    std::map<uint32_t, uint32_t> m_counters;
    uint32_t m_num_counters;
    uint32_t m_act_threshold;
};

struct RowHammerConfig {
    enum class Mode { Graphene, PARA };
    Mode mode = Mode::Graphene;

    int banks_per_rank = 16;
    int rows_per_sa = 1;

    // Graphene: counters per bank and activations per tracked row between neighbor refreshes
    uint32_t num_counters = 0;
    uint32_t act_threshold = 0;
    int64_t refresh_window = 0;

    // PARA: probability (percent) to refresh the neighbors of an activated row
    double neighbor_refresh_pct = 1;
    uint32_t seed = 0;

    int latency = 80;
    int blast_radius = 1;
    int queue_size = 1024;
};

class RowHammerProtection {
public:
    using ActiveFn = RankEngine::ActiveFn;

    RowHammerProtection(const RowHammerConfig& cfg, int rank, LockTable& locks, ActiveFn is_sa_active)
        : m_cfg(cfg), m_bank_base(rank * cfg.banks_per_rank), m_locks(locks), m_is_sa_active(std::move(is_sa_active)),
          m_gen(cfg.seed), m_distribution({100 - cfg.neighbor_refresh_pct, cfg.neighbor_refresh_pct}) {
        if (cfg.mode == RowHammerConfig::Mode::Graphene) {
            if (cfg.act_threshold == 0) {
                throw std::invalid_argument("SMD: smd_rh_protection_mac must be at least 2");
            }
            m_tables.assign(cfg.banks_per_rank, GrapheneCounterTable(cfg.num_counters, cfg.act_threshold));
        }
        // Neighbors on both sides of the aggressor are refreshed by one maintenance operation
        m_rows_per_op = cfg.blast_radius * 2;
    }

    void on_activation(int bank_in_rank, int sa, int row) {
        bool aggressor = false;
        if (m_cfg.mode == RowHammerConfig::Mode::PARA) {
            aggressor = m_distribution(m_gen) != 0;
        } else {
            aggressor = m_tables[bank_in_rank].increment(static_cast<uint32_t>(sa) * m_cfg.rows_per_sa + row);
        }
        if (aggressor) {
            enqueue({bank_in_rank, sa, row});
        }
    }

    void tick() {
        m_clk++;
        if (m_cfg.mode == RowHammerConfig::Mode::Graphene && m_clk % m_cfg.refresh_window == 0) {
            for (auto& table : m_tables) {
                table.reset();
            }
        }

        if (m_completion_clk > m_clk) {
            return; // busy
        }
        if (m_completion_clk == m_clk) {
            m_locks.release(m_locked.bank + m_bank_base, m_locked.sa);
        }
        if (m_queue.empty()) {
            return;
        }

        const RowAddr& aggressor = m_queue.front();
        const int bank = aggressor.bank + m_bank_base;
        if (m_is_sa_active(bank, aggressor.sa) || m_locks.has_lock(bank)) {
            m_completion_clk = 0;  // retry next cycle
            return;
        }
        m_locks.lock(bank, aggressor.sa);
        m_locked = aggressor;
        m_completion_clk = m_clk + static_cast<int64_t>(m_cfg.latency) * m_rows_per_op;
        m_queue.pop_front();
        m_num_done++;
    }

    uint64_t num_issued() const { return m_num_issued; }
    uint64_t num_done() const { return m_num_done; }
    void reset_stats() { m_num_issued = m_num_done = 0; }

private:
    void enqueue(const RowAddr& aggressor) {
        // The row's neighbors are already due
        if (std::find(m_queue.begin(), m_queue.end(), aggressor) != m_queue.end()) {
            return;
        }
        if (m_queue.size() >= static_cast<size_t>(m_cfg.queue_size)) {
            throw std::runtime_error("SMD: more than " + std::to_string(m_cfg.queue_size) +
                                     " aggressor rows wait for their neighbors to be refreshed: the neighbor refreshes cannot keep up; "
                                     "raise smd_rh_protection_neighbor_ref_queue_size or smd_rh_protection_mac");
        }
        m_queue.push_back(aggressor);
        m_num_issued++;
    }

    RowHammerConfig m_cfg;
    int m_bank_base;
    LockTable& m_locks;
    ActiveFn m_is_sa_active;
    int m_rows_per_op = 2;

    std::vector<GrapheneCounterTable> m_tables; // per bank of the rank (Graphene)
    std::mt19937 m_gen;
    std::discrete_distribution<uint64_t> m_distribution;

    int64_t m_clk = 0;
    std::deque<RowAddr> m_queue;
    int64_t m_completion_clk = -1;
    RowAddr m_locked;

    uint64_t m_num_issued = 0;
    uint64_t m_num_done = 0;
};

}  // namespace Ramuator::SMD

#endif  // RAMUATOR_CONTROLLER_SMD_ROWHAMMER_H