#ifndef RAMULATOR_CONTROLLER_SMD_ROWHAMMER_H
#define RAMULATOR_CONTROLLER_SMD_ROWHAMMER_H

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/controller/smd/maintenance.h"

namespace Ramulator::SMD {
/*
    RowHammer protection inside the DRAM chip (SMD-DRP / SMD-PRP). A detector watches the activations the chip serves,
    and when a row looks like a agressor the chip refreshes the agressor's neighbors on its own: it locks the aggressors'
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
    // `legacy_eviction` reproduces the replacement rule of the previous SMD simulator (see smallest_entry())
    GrapheneCounterTable(uint32_t num_counters, uint32_t act_threshold, bool legacy_eviction = false)
        : m_num_counters(num_counters), m_act_threshold(act_threshold), m_legacy_eviction(legacy_eviction) {}
    
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
        if (m_legacy_eviction) {
            return std::min_element(m_counters.begin(), m_counters.end());
        }
        return std::min_element(m_counters.begin(), m_counters.end(),
                                [](const auto& a, const auto& b) { return a.second < b.second; });
    }

    uint32_t m_spillover = 0;
    std::map<uint32_t, uint32_t> m_counters;
    uint32_t m_num_counters;
    uint32_t m_act_threshold;
    bool m_legacy_eviction;
};

// Detector of one rank on one chip: Graphene.
class GrapheneDetector {
public:
    GrapheneDetector(int banks_per_rank, int rows_per_sa, uint32_t num_counters, uint32_t act_threshold, int64_t refresh_window, bool legacy_eviction = false)
                    : m_rows_per_sa(rows_per_sa), m_refresh_window(refresh_window) {
        if (act_threshold == 0) {
            throw std::invalid_argument("SMD: Graphene's mac must be at least 2");
        }
        m_tables.assign(banks_per_rank, GrapheneCounterTable(num_counters, act_threshold, legacy_eviction));
    }

    bool on_activation(int bank_in_rank, int sa, int row) {
        return m_tables[bank_in_rank].increment(static_cast<uint32_t>(sa) * m_rows_per_sa + row);
    }

    void tick() {
        if (++m_clk % m_refresh_window == 0) {
            for (auto& table : m_tables) {
                table.reset();
            }
        }
    }

private:
    int m_rows_per_sa;
    int64_t m_refresh_window;
    int64_t m_clk = 0;
    std::vector<GrapheneCounterTable> m_tables;
};

// Detector of one rank on the chip: PARA.
class ParaDetector {
public:
    ParaDetector(double probability_pct, uint32_t seed) : m_gen(seed), m_distribution({100 - probability_pct, probability_pct}) {}
    bool on_activation(int, int, int) { return m_distribution(m_gen) != 0; }
    void tick() {}

private:
    std::mt19937 m_gen;
    std::discrete_distribution<uint64_t> m_distribution;
};

struct NeighborRefreshConfig {
    int latency = 80;
    int blast_radius = 1;
    int queue_size = 1024;
};

class NeighborRefresher {
public:
    NeighborRefresher(const NeighborRefreshConfig& cfg, int rank, int banks_per_rank, LockTable& locks, RankEngine::ActiveFn is_sa_active)
        : m_cfg(cfg), m_bank_base(rank * banks_per_rank), m_locks(locks), m_is_sa_active(std::move(is_sa_active)), m_rows_per_op(cfg.blast_radius * 2) {}

    void enqueue(const RowAddr& aggressor) {
        if (std::find(m_queue.begin(), m_queue.end(), aggressor) != m_queue.end()) {
            return;
        }
        if (m_queue.size() >= static_cast<size_t>(m_cfg.queue_size)) {
            throw std::runtime_error("SMD: more than " + std::to_string(m_cfg.queue_size) +
                                     " aggressor rows wait for their neighbors to be refreshed: the neighbor refreshes "
                                     "cannot keep up; raise the neighbor refresh queue size or the detection threshold");
        }
        m_queue.push_back(aggressor);
        m_num_issued++;
    }

    void tick() {
        m_clk++;

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
    NeighborRefreshConfig m_cfg;
    int m_bank_base;
    LockTable& m_locks;
    RankEngine::ActiveFn m_is_sa_active;
    int m_rows_per_op;

    int64_t m_clk = 0;
    std::deque<RowAddr> m_queue;
    int64_t m_completion_clk = -1;
    RowAddr m_locked;

    uint64_t m_num_issued = 0;
    uint64_t m_num_done = 0;
};

template <class Detector>
class RowHammerChips {
public:
    template <class MakeDetector>
    void build(const SMDContext& ctx, const NeighborRefreshConfig& cfg, MakeDetector make_detector) {
        m_num_ranks = ctx.num_ranks;
        m_banks_per_rank = ctx.banks_per_rank;
        for (int chip = 0; chip < ctx.num_chips; chip++) {
            for (int rank = 0; rank < ctx.num_ranks; rank++) {
                m_chips.push_back({make_detector(rank, chip), NeighborRefresher(cfg, rank, ctx.banks_per_rank, *ctx.locks[chip], ctx.is_sa_active)});
            }
        }
    }

    void tick() {
        for (auto& chip : m_chips) {
            chip.detector.tick();
            chip.refresher.tick();
        }
    }

    void on_activation(int bank, int sa, int row) {
        const int rank = bank / m_banks_per_rank;
        const int bank_in_rank = bank % m_banks_per_rank;
        const int num_chips = static_cast<int>(m_chips.size()) / m_num_ranks;
        for (int chip = 0; chip < num_chips; chip++) {
            auto& c = m_chips[chip * m_num_ranks + rank];
            if (c.detector.on_activation(bank_in_rank, sa, row)) {
                c.refresher.enqueue({bank_in_rank, sa, row});
            }
        }
    }

    void collect(SMDStatMap& out) const {
        for (int rank = 0; rank < m_num_ranks && rank < static_cast<int>(m_chips.size()); rank++) {
            out["neighbor_refs_issued"] += m_chips[rank].refresher.num_issued();
            out["neighbor_refs_done"] += m_chips[rank].refresher.num_done();
        }
    }

    void reset() {
        for (auto& chip : m_chips) {
            chip.refresher.reset_stats();
        }
    }

private:
    struct Chip {
        Detector detector;
        NeighborRefresher refresher;
    };

    int m_num_ranks = 1;
    int m_banks_per_rank = 1;
    std::vector<Chip> m_chips;
};

}  // namespace Ramuator::SMD

#endif  // RAMUATOR_CONTROLLER_SMD_ROWHAMMER_H