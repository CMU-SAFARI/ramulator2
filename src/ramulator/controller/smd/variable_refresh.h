#ifndef RAMULATOR_CONTROLLER_SMD_VARIABLE_REFRESH_H
#define RAMULATOR_CONTROLLER_SMD_VARIABLE_REFRESH_H

#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "ramulator/controller/smd/maintenance.h"

namespace Ramulator::SMD {

class BloomFilter {
public:
    BloomFilter(uint32_t size, uint32_t num_hashes, uint32_t seed = 0) : m_entries(size, false) {
        if (size == 0 || (size & (size - 1)) != 0) {
            throw std::invalid_argument("SMD: the Bloom filter size must be a power of two");
        }
        if (num_hashes == 0) {
            throw std::invalid_argument("SMD: a Bloom filter needs at least one hash function");
        }
        std::mt19937 gen(seed);
        std::uniform_int_distribution<uint32_t> distrib(0, size - 1);
        m_q.assign(num_hashes, std::vector<uint32_t>(kKeyBits));
        for (auto& q : m_q) {
            for (auto& word : q) {
                word = distrib(gen);
            }
        }
    }

    void insert(uint32_t key) {
        for (uint32_t i = 0; i < m_q.size(); i++) {
            m_entries[hash(key, i)] = true;
        }
        m_shadow.insert(key);
    }

    bool test(uint32_t key) {
        for (uint32_t i = 0; i < m_q.size(); i++) {
            if (!m_entries[hash(key, i)]) {
                m_negatives++;
                return false;
            }
        }
        m_positives++;
        if (m_shadow.count(key) == 0) {
            m_false_positives++;
        }
        return true;
    }

    uint64_t positives() const { return m_positives; }
    uint64_t negatives() const { return m_negatives; }
    uint64_t false_positives() const { return m_false_positives; }
    void reset_stats() { m_positives = m_negatives = m_false_positives = 0; }

private:
    static constexpr uint32_t kKeyBits = 32;

    uint32_t hash(uint32_t key, uint32_t func) const {
        uint32_t addr = 0;
        for (uint32_t word : m_q[func]) {
            if (key & 1) {
                addr ^= word;
            }
            key >>= 1;
        }
        return addr;
    }

    std::vector<bool> m_entries;
    std::vector<std::vector<uint32_t>> m_q;
    std::unordered_set<uint32_t> m_shadow;
    uint64_t m_positives = 0;
    uint64_t m_negatives = 0;
    uint64_t m_false_positives = 0;
};

struct VariableRefreshConfig {
    std::string distribution = "discrete";
    double weak_row_percentage = 0.05;
    uint32_t bloom_size = 8192;
    uint32_t bloom_hashes = 6;
    double refresh_period_ms = 32;
    uint32_t seed = 0;
};

class VariableRefreshPolicy : public RefreshPolicy {
public:
    static constexpr uint32_t kRelaxingFactor = 4;

    VariableRefreshPolicy(const VariableRefreshConfig& vc, const EngineConfig& ec, int rank)
        : m_filter(vc.bloom_size, vc.bloom_hashes),
          m_bank_base(rank * ec.banks_per_rank),
          m_rows_per_sa(ec.rows_per_sa) {
        if (ec.banks_per_rank > 16) {
            throw std::invalid_argument("SMD: variable refresh supports at most 16 banks per rank");
        }

        const double relaxed_period_ms = std::ceil(vc.refresh_period_ms) * kRelaxingFactor;
        std::mt19937 gen(vc.seed);
        std::normal_distribution<> normal{400, 70};
        std::discrete_distribution<uint64_t> discrete({100 - vc.weak_row_percentage, vc.weak_row_percentage});
        const bool use_discrete = vc.distribution == "discrete";
        if (!use_discrete && vc.distribution != "normal") {
            throw std::invalid_argument("SMD: unknown smd_variable_refresh_distribution \"" + vc.distribution + "\" (expected discrete or normal)");
        }

        for (int bank = 0; bank < ec.banks_per_rank; bank++) {
            for (uint32_t row = 0; row < static_cast<uint32_t>(ec.rows_per_sa) * ec.num_sas; row++) {
                const bool weak = use_discrete ? discrete(gen) == 1 : normal(gen) < relaxed_period_ms;
                if (weak) {
                    m_filter.insert(key(bank, row));
                    m_num_weak++;
                }
            }
        }
        m_filter.reset_stats();
    }

    uint32_t rows_to_refresh(const MaintenanceCounter& mc, uint32_t granularity) override {
        if (mc.rollbacks % kRelaxingFactor == kRelaxingFactor - 1) {
            return granularity;
        }
        const int bank = mc.bank - m_bank_base;
        uint32_t rows = 0;
        for (uint32_t row = mc.row_counter; row < mc.row_counter + granularity; row++) {
            if (m_filter.test(key(bank, mc.sa_counter * m_rows_per_sa + row))) {
                rows++;
            }
        }
        return rows;
    }

    uint64_t num_weak_rows() const { return m_num_weak; }
    const BloomFilter& filter() const { return m_filter; }
    void reset_stats() { m_filter.reset_stats(); }
    
private:
    static uint32_t key(int bank, uint32_t row_id) {
        return (row_id << 4) + bank;
    }

    BloomFilter m_filter;
    int m_bank_base;
    uint32_t m_rows_per_sa;
    uint64_t m_num_weak = 0;
};

}  // namespace Ramulator::SMD

#endif  // RAMULATOR_CONTROLLER_SMD_VARIABLE_REFRESH_H