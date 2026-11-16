#ifndef RAMULATOR_CONTROLLER_SMD_MAINTENANCE_H
#define RAMULATOR_CONTROLLER_SMD_MAINTENANCE_H

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Ramulator::SMD {

struct RegionLock{
    bool locked = false;
    int sa_id = -1;
    bool bank_locked = false;
};

class LockTable {
public:
    explicit LockTable(int num_banks) : m_locks(num_banks) {}

    bool under_maintenance(int bank, int sa) const {
        const RegionLock& l = m_locks[bank];
        return (l.locked && l.sa_id == sa) || l.bank_locked;
    }

    bool has_lock(int bank) const {
        return m_locks[bank].locked;
    }

    void lock(int bank, int sa, bool entire_bank = false) {
        RegionLock& l = m_locks[bank];
        if (l.locked) {
            throw std::logic_error("SMD: bank " + std::to_string(bank) + " already has a locked subarray");
        }
        l = {true, sa, entire_bank};
    }

    void release(int bank, int sa) {
        RegionLock& l = m_locks[bank];
        if (!l.locked || l.sa_id != sa) {
            throw std::logic_error("SMD: releasing subarray " + std::to_string(sa) + " of bank " + std::to_string(bank) + " that is not the locked one");
        }
        l.locked = false;
        l.bank_locked = false;
    }

private:
    std::vector<RegionLock> m_locks;
};

struct MaintenanceCounter {
    int bank = 0;
    uint32_t row_counter = 0;
    uint32_t sa_counter = 0;
    uint64_t rollbacks = 0;
    uint32_t pending = 0;

    void increment(uint32_t num_sas, uint32_t num_rows, uint32_t granularity) {
        if (sa_counter == num_sas - 1) {
            sa_counter = 0;
            uint32_t old_row = row_counter;
            row_counter = (row_counter + granularity) % num_rows;
            if (old_row > row_counter) {
                rollbacks++;
            }
            return;
        }
        sa_counter++;
    }
};

struct EngineConfig {
    int num_ranks = 1;
    int banks_per_rank = 16;
    int num_sas = 1;
    int rows_per_sa = 1;
    int64_t refresh_period = 0;
    int latency = 80;
    int auto_latency = 80;
    int granularity = 8;
    int num_machines = 4;
    int pending_limit = 9;
    bool worst_case_distribution = false;
};

class RefreshPolicy {
public:
    virtual ~RefreshPolicy() = default;
    virtual uint32_t rows_to_refresh(const MaintenanceCounter& mc, uint32_t granularity) = 0;
    virtual void on_maintenance(const MaintenanceCounter& mc, uint32_t rows) {}
};

class FixedRatePolicy : public RefreshPolicy {
public:
    uint32_t rows_to_refresh(const MaintenanceCounter&, uint32_t granularity) override {
        return granularity;
    }
};

class RankEngine {
public:
    using ActiveFn = std::function<bool(int bank, int sa)>;
    RankEngine(const EngineConfig& cfg, int rank, int chip, LockTable& locks, ActiveFn is_sa_active, RefreshPolicy& policy)
              : m_cfg(cfg), m_locks(locks), m_is_sa_active(std::move(is_sa_active)), m_policy(policy) {
        m_latency = cfg.latency > 0 ? cfg.latency : cfg.auto_latency;
        
        int64_t rows_total = static_cast<int64_t>(cfg.rows_per_sa) * cfg.num_sas;
        m_ref_interval = cfg.refresh_period / rows_total;
        if (m_ref_interval <= m_latency) {
            throw std::invalid_argument(
                "SMD: the maintenance latency (" + std::to_string(m_latency) + " cycles) must be shorter than the interval between new rows to refresh ("
                + std::to_string(m_ref_interval) + " cycles); raise smd_refresh_period"
            );
        }
        m_ref_interval *= cfg.granularity;
        if (cfg.worst_case_distribution) {
            m_ref_offset = static_cast<int64_t>(chip) * m_latency * cfg.granularity;
        }

        if (cfg.banks_per_rank % cfg.num_machines != 0) {
            throw std::invalid_argument("SMD: smd_num_ref_machines must divide the number of banks per rank");
        }
        m_machines.resize(cfg.num_machines);
        for (int b = 0; b < cfg.banks_per_rank; b++) {
            MaintenanceCounter mc;
            mc.bank = rank * cfg.banks_per_rank + b;
            if (cfg.worst_case_distribution) {
                mc.sa_counter = chip % cfg.num_sas;
            }
            m_machines[b % cfg.num_machines].counters.push_back(mc);
        }
    }

    int64_t ref_interval() const { return m_ref_interval; }
    uint64_t num_maintenance_ops() const { return m_num_ops; }
    uint64_t num_rows_refreshed() const { return m_num_rows; }
    uint32_t max_pending() const { return m_max_pending; }

    void tick() {
        m_clk++;
        for (auto& m : m_machines) {
            tick_machine(m);
        }
        if ((m_clk + 1 + m_ref_offset) % m_ref_interval == 0) {
            for (auto& m : m_machines) {
                for (auto& mc : m.counters) {
                    mc.pending++;
                    if (mc.pending > static_cast<uint32_t>(m_cfg.pending_limit)) {
                        throw std::runtime_error(
                            "SMD: bank " +std::to_string(mc.bank) + " owes " + std::to_string(mc.pending) +
                            " maintenance operations: the maintenance machines cannot keep up with the refresh demand; "
                            "raise smd_num_ref_machines or smd_refresh_period"
                        );
                    }
                    if (mc.pending > m_max_pending) {
                        m_max_pending = mc.pending;
                    }
                }
            }
        }
    }

private:
    struct Machine {
        std::vector<MaintenanceCounter> counters;
        size_t next = 0;
        int64_t completion_clk = -1;
        MaintenanceCounter locked_region;
    };

    void tick_machine(Machine& m) {
        if (m.completion_clk > m_clk) {
            return;
        }
        if (m.completion_clk == m_clk) {
            m_locks.release(m.locked_region.bank, m.locked_region.sa_counter);
        }

        MaintenanceCounter& mc = m.counters[m.next];
        m.next = (m.next + 1) % m.counters.size();
        if (mc.pending == 0) {
            return;
        }

        uint32_t rows = m_policy.rows_to_refresh(mc, m_cfg.granularity);
        if (rows != 0) {
            if (m_is_sa_active(mc.bank, mc.sa_counter) || m_locks.has_lock(mc.bank)) {
                m.completion_clk = 0;
                return;
            }
            m_locks.lock(mc.bank, mc.sa_counter);
            m.locked_region = mc;
            m.completion_clk = m_clk + static_cast<int64_t>(m_latency) * rows;
            m_num_ops++;
            m_num_rows += rows;
            m_policy.on_maintenance(mc, rows);
        }

        mc.increment(m_cfg.num_sas, m_cfg.rows_per_sa, m_cfg.granularity);
        mc.pending--;
    }

    EngineConfig m_cfg;
    LockTable& m_locks;
    ActiveFn m_is_sa_active;
    RefreshPolicy& m_policy;

    int m_latency = 0;
    int64_t m_ref_interval = 0;
    int64_t m_ref_offset = 0;
    int64_t m_clk = 0;
    std::vector<Machine> m_machines;

    uint64_t m_num_ops = 0;
    uint64_t m_num_rows = 0;
    uint32_t m_max_pending = 0;
};

}  // namespace Ramulator::SMD

#endif  // RAMULATOR_CONTROLLER_SMD_MAINTENANCE_H