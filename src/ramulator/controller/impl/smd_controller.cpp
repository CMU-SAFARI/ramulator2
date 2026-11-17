#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/controller/refresh/i_refresh_manager.h"
#include "ramulator/controller/rowpolicy/i_row_policy.h"
#include "ramulator/controller/smd/maintenance.h"
#include "ramulator/controller/smd/rowhammer.h"
#include "ramulator/controller/smd/variable_refresh.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/*
  Controller for Self-Managing DRAM (SMD): the DRAM refreshes itself, one subarray at a time,
  and the controller issues no refresh commands. An ACT to a subarray under maintenance is rejected:

    cycle t                             the controller issues the act (all ACT timings are charged)
    cycle t + nACTtoNACK                the DRAM answers ACT_NACK; the bank closes again
    cycle t + nACTtoNACK + nNACK_RESEND the subarray accepts an ACT again after nNACK_RESEND (Subarray-level timing);
                                        the request stays at the controller and retries by itself
    
  The maintenance schedule and region locks live in SMD::RankEngine (controllersmd/maintenance.h),
  one per rank and chip: the DRAM chips of a rank refresh on their own, so an ACT may b eNACKed by all the chips of
  the rank or by only some of them (a partial NACK; modeled like a full one). Chips run in lockstep under fixed-rate
  refresh and are simulated as one; under variable-rate refresh, where each chip refreshes its own weak rows,
  they are simulated individually. Needs a standard with a Subarray level and ACT_NACK (e.g., DDR4_SMD); pair it with
  the NoRefresh refresh manager, since the DRAM refreshes on its own.
*/
class SMDController : public ControllerBase {
    RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, SMDController, ControllerBase, "SMD")

public:
    void init() override {
        init_base();

        RAMULATOR_PARSE_PARAM(m_ref_policy, std::string, "smd_ref_policy").default_val("FixedRate");
        // Cycles of one full refresh sweep over a bank's rows (default: 64 ms at 1600 MHz)
        RAMULATOR_PARSE_PARAM(m_refresh_period, int64_t, "smd_refresh_period").default_val(102400000);
        RAMULATOR_PARSE_PARAM(m_num_machines, int, "smd_num_ref_machines").default_val(4);
        RAMULATOR_PARSE_PARAM(m_granularity, int, "smd_row_refresh_granularity").default_val(8);
        // cycles to refresh one row; <= 0 derives it from nRFC
        RAMULATOR_PARSE_PARAM(m_latency, int, "smd_single_ref_latency").default_val(80);
        RAMULATOR_PARSE_PARAM(m_pending_limit, int, "smd_pending_ref_limit").default_val(9);
        // A row open for more than (timeout ratio x refresh interval x max intervals) cycles is
        // precharged by the controller so that it cannot starve its subarray of maintenance
        RAMULATOR_PARSE_PARAM(m_max_row_open_intervals, int, "smd_max_row_open_intervals").default_val(8);
        RAMULATOR_PARSE_PARAM(m_timeout_to_interval_ratio, float, "smd_timeout_to_ref_interval_ratio").default_val(0.5f);
        RAMULATOR_PARSE_PARAM(m_worst_case_distribution, bool, "smd_worst_case_ref_distribution").default_val(false);
        // Variable-rate refresh: how weak rows are drawn ("discrete" or "normal"), the weak share (percent), the Bloom filter
        RAMULATOR_PARSE_PARAM(m_vr_distribution, std::string, "smd_variable_refresh_distribution").default_val("discrete");
        RAMULATOR_PARSE_PARAM(m_vr_weak_row_percentage, float, "smd_variable_refresh_weak_row_percentage").default_val(0.05f);
        RAMULATOR_PARSE_PARAM(m_vr_bloom_size, int, "smd_variable_refresh_bloom_filter_size").default_val(8192);
        RAMULATOR_PARSE_PARAM(m_vr_bloom_hashes, int, "smd_variable_refresh_bloom_filter_hashes").default_val(6);
        // RowHammer protection: "Graphene" (deterministic) or "PARA" (probabilistic)
        RAMULATOR_PARSE_PARAM(m_rh_enabled, bool, "smd_rh_protection_enabled").default_val(false);
        RAMULATOR_PARSE_PARAM(m_rh_mode, std::string, "smd_rh_protection_mode").default_val("Graphene");
        RAMULATOR_PARSE_PARAM(m_rh_mac, int, "smd_rh_protection_mac").default_val(8192);
        RAMULATOR_PARSE_PARAM(m_rh_neighbor_refresh_pct, float, "smd_rh_neighbor_refresh_pct").default_val(1.0f);
        RAMULATOR_PARSE_PARAM(m_rh_blast_radius, int, "smd_rh_blast_radius").default_val(1);
        RAMULATOR_PARSE_PARAM(m_rh_queue_size, int, "smd_rh_protection_neighbor_ref_queue_size").default_val(1024);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        setup_base(frontend, memory_system);

        if (m_ref_policy == "Disabled") {
            m_enabled = false;
        } else if (m_ref_policy == "FixedRate" || m_ref_policy == "VariableRefresh" || m_ref_policy == "NoRefresh") {
            m_enabled = true;
        } else {
            throw std::invalid_argument("SMDController: unknown smd_ref_policy \"" + m_ref_policy + "\" (expected FixedRate, VariableRefresh, NoRefresh, or Disabled)");
        }
        if (m_rh_enabled && m_rh_mode != "Graphene" && m_rh_mode != "PARA") {
            throw std::invalid_argument("SMDController: unsupported smd_rh_protection_mode \"" + m_rh_mode + "\" (expected Graphene or PARA)");
        }
        if (m_rh_enabled && !m_enabled) {
            throw std::invalid_argument("SMDController: RowHammer protection needs SMD (smd_ref_policy is Disabled)");
        }

        auto* spec = m_device.m_spec;
        if (!spec->has_level("Subarray") || !spec->has_command("ACT_NACK")) {
            throw std::runtime_error("SMDController requires a DRAM standard with a Subarray level and the ACT_NACK command (e.g., DDR4_SMD)!");
        }

        m_cmd_act = spec->get_command_id("ACT");
        m_cmd_act_nack = spec->get_command_id("ACT_NACK");
        m_subarray_level = spec->get_level_id("Subarray");
        m_row_level = spec->get_level_id("Row");
        m_num_ranks = spec->get_level_size("Rank");
        m_banks_per_rank = m_device.m_bank_nodes.size() / m_num_ranks;
        m_nACTtoNACK = spec->get_timing_value("nACTtoNACK");

        const int num_banks = m_device.m_bank_nodes.size();
        m_act_clk.assign(num_banks, 0);
        m_prepb_injected.assign(num_banks, 0);
        m_cmd_prepb = spec->get_command_id("PREpb");

        m_stats.add("smd_act_nacks", s_act_nacks);
        m_stats.add("smd_act_partial_nacks", s_act_partial_nacks);
        m_stats.add("smd_bloom_positives", s_bloom_positives);
        m_stats.add("smd_bloom_false_positives", s_bloom_false_positives);
        m_stats.add("smd_forced_precharges", s_forced_precharges);
        m_stats.add("smd_maintenance_ops", s_maintenance_ops);
        m_stats.add("smd_rows_refreshed", s_rows_refreshed);
        m_stats.add("smd_max_pending_maintenance", s_max_pending);
        m_stats.add("smd_neighbor_refs_issued", s_neighbor_refs_issued);
        m_stats.add("smd_neighbor_refs_done", s_neighbor_refs_done);

        if (!m_enabled) {
            return;
        }

        SMD::EngineConfig cfg;
        cfg.num_ranks = spec->get_level_size("Rank");
        cfg.banks_per_rank = num_banks / cfg.num_ranks;
        cfg.num_sas = spec->get_level_size("Subarray");
        cfg.rows_per_sa = spec->get_level_size("Row");
        cfg.refresh_period = m_refresh_period;
        cfg.latency = m_latency;
        if (m_latency <= 0) {
            // Spread the all-bank refresh time (nRFC per 8192 REF commands) over a bank's rows
            const int rows_per_bank = cfg.num_sas * cfg.rows_per_sa;
            cfg.auto_latency = std::ceil(spec->get_timing_value("nRFC") * 8192.0 / rows_per_bank);
            m_logger.info(fmt::format("Automatically setting the SMD maintenance latency to {} cycles", cfg.auto_latency));
        }
        cfg.granularity = m_granularity;
        cfg.num_machines = m_num_machines;
        cfg.pending_limit = m_pending_limit;
        cfg.worst_case_distribution = m_worst_case_distribution;

        const bool variable = m_ref_policy == "VariableRefresh";
        const bool refreshing = m_ref_policy != "NoRefresh";
        const bool para = m_rh_enabled && m_rh_mode == "PARA";
        // Chips differ (and need their own locks) only when they refresh different rows or are staggered, or draw their own random numbers
        const int chips_per_rank = spec->channel_width / spec->organization.dq;
        m_num_chips = (variable || m_worst_case_distribution || para) ? chips_per_rank : 1;

        m_state_opened = spec->get_state_id("Opened");
        auto is_sa_active = [this](int bank, int sa) {
            const DRAMNode* subarray = m_device.m_bank_nodes[bank]->m_child_nodes[sa].get();
            return subarray->m_state == m_state_opened;
        };

        SMD::VariableRefreshConfig vc;
        vc.distribution = m_vr_distribution;
        vc.weak_row_percentage = m_vr_weak_row_percentage;
        vc.bloom_size = m_vr_bloom_size;
        vc.bloom_hashes = m_vr_bloom_hashes;
        // The sweep time in ms, for the retention-time distribution (tCK in ps)
        vc.refresh_period_ms = std::ceil(m_refresh_period * (m_tCK_ps / 1000.0) / 1e6);

        SMD::RowHammerConfig rc;
        if (m_rh_enabled) {
            rc.mode = m_rh_mode == "PARA" ? SMD::RowHammerConfig::Mode::PARA : SMD::RowHammerConfig::Mode::Graphene;
            rc.banks_per_rank = cfg.banks_per_rank;
            rc.rows_per_sa = cfg.rows_per_sa;
            rc.neighbor_refresh_pct = m_rh_neighbor_refresh_pct;
            rc.latency = m_latency > 0 ? m_latency : cfg.auto_latency;
            rc.blast_radius = m_rh_blast_radius;
            rc.queue_size = m_rh_queue_size;
            rc.refresh_window = m_refresh_period;
            // Graphene sizes its counter tables for the activations a bank can serve in a refresh window
            const float nRFC = spec->get_timing_value("nRFC");
            const float nREFI = spec->get_timing_value("nREFI");
            const uint32_t nRC = spec->get_timing_value("nRC");
            const auto acts_per_window = static_cast<uint32_t>(
                std::floor((static_cast<uint32_t>(m_refresh_period) * (1 - nRFC / nREFI)) / nRC)
            );
            rc.act_threshold = m_rh_mac / 2;
            rc.num_counters = rc.act_threshold > 0 ? acts_per_window / rc.act_threshold : 0;
        }

        for (int chip = 0; chip < m_num_chips; chip++) {
            m_locks.push_back(std::make_unique<SMD::LockTable>(num_banks));
            for (int rank = 0; rank < cfg.num_ranks; rank++) {
                if (m_rh_enabled) {
                    rc.seed = rank * chips_per_rank + chip;
                    m_rh.push_back(std::make_unique<SMD::RowHammerProtection>(rc, rank, *m_locks[chip], is_sa_active));
                }
                if (!refreshing) {
                    continue;
                }
                if (variable) {
                    vc.seed = rank * chips_per_rank + chip;
                    auto policy = std::make_unique<SMD::VariableRefreshPolicy>(vc, cfg, rank);
                    m_vr_policies.push_back(policy.get());
                    m_policies.push_back(std::move(policy));
                } else {
                    m_policies.push_back(std::make_unique<SMD::FixedRatePolicy>());
                }
                m_engines.push_back(std::make_unique<SMD::RankEngine>(cfg, rank, chip, *m_locks[chip], is_sa_active, *m_policies.back()));
            }
        }
        const int64_t ref_interval =
            m_refresh_period / (static_cast<int64_t>(cfg.rows_per_sa) * cfg.num_sas) * cfg.granularity;
        m_force_precharge_after = 
            static_cast<int64_t>(std::floor(ref_interval * m_timeout_to_interval_ratio)) * m_max_row_open_intervals;
    }

    void tick() override;

    void update_stats() override {
        ControllerBase::update_stats();
        uint64_t ops = 0, rows = 0;
        uint32_t pending = 0;
        const size_t chip0_engines = m_engines.size() / std::max(m_num_chips, 1);
        for (size_t i = 0; i < m_engines.size(); i++) {
            if (i < chip0_engines) {
                ops += m_engines[i]->num_maintenance_ops();
                rows += m_engines[i]->num_rows_refreshed();
            }
            pending = std::max(pending, m_engines[i]->max_pending());
        }
        s_maintenance_ops = ops - m_ops_base;
        s_rows_refreshed = rows - m_rows_base;
        s_max_pending = pending;
        s_neighbor_refs_issued = s_neighbor_refs_done = 0;
        for (size_t i = 0; i < m_rh.size() / std::max(m_num_chips, 1); i++) {
            s_neighbor_refs_issued += m_rh[i]->num_issued();
            s_neighbor_refs_done += m_rh[i]->num_done();
        }
        s_bloom_positives = s_bloom_false_positives = 0;
        for (auto* vr : m_vr_policies) {
            s_bloom_positives += vr->filter().positives();
            s_bloom_false_positives += vr->filter().false_positives();
        }
    }

    void reset_stats() override {
        ControllerBase::reset_stats();
        s_act_nacks = 0;
        s_forced_precharges = 0;
        s_maintenance_ops = 0;
        s_rows_refreshed = 0;
        s_act_partial_nacks = 0;
        m_ops_base = m_rows_base = 0;
        const size_t chip0_engines = m_engines.size() / std::max(m_num_chips, 1);
        for (size_t i = 0; i < chip0_engines; i++) {
            m_ops_base += m_engines[i]->num_maintenance_ops();
            m_rows_base += m_engines[i]->num_rows_refreshed();
        }
        for (auto* vr : m_vr_policies) {
            vr->reset_stats();
        }
        for (auto& rh : m_rh) {
            rh->reset_stats();
        }
        s_neighbor_refs_issued = s_neighbor_refs_done = 0;
    }

private:
    struct PendingNack {
        Clk_t due;
        AddrVec_t addr_vec;
    };

    // Config
    std::string m_ref_policy;
    int64_t m_refresh_period = 0;
    int m_num_machines = 0;
    int m_granularity = 0;
    int m_latency = 0;
    int m_pending_limit = 0;
    int m_max_row_open_intervals = 0;
    float m_timeout_to_interval_ratio = 0;
    bool m_worst_case_distribution = false;
    std::string m_vr_distribution;
    float m_vr_weak_row_percentage = 0;
    int m_vr_bloom_size = 0;
    int m_vr_bloom_hashes = 0;
    bool m_rh_enabled = false;
    std::string m_rh_mode;
    int m_rh_mac = 0;
    float m_rh_neighbor_refresh_pct = 0;
    int m_rh_blast_radius = 0;
    int m_rh_queue_size = 0;
    bool m_enabled = false;
    int m_num_chips = 1;

    // Spec lookups
    int m_cmd_act = -1;
    int m_cmd_act_nack = -1;
    int m_subarray_level = -1;
    int m_row_level = -1;
    int m_num_ranks = 1;
    int m_banks_per_rank = 1;
    int m_state_opened = -1;
    int m_nACTtoNACK = 0;
    int m_cmd_prepb = -1;

    // Maintenance (DRAM side)
    std::vector<std::unique_ptr<SMD::LockTable>> m_locks;
    std::vector<std::unique_ptr<SMD::RefreshPolicy>> m_policies;
    std::vector<SMD::VariableRefreshPolicy*> m_vr_policies;
    std::vector<std::unique_ptr<SMD::RankEngine>> m_engines;
    std::vector<std::unique_ptr<SMD::RowHammerProtection>> m_rh;

    std::deque<PendingNack> m_pending_nacks;
    std::vector<Clk_t> m_act_clk;
    std::vector<char> m_prepb_injected;
    int64_t m_force_precharge_after = 0;

    // Stats
    size_t s_act_nacks = 0;
    size_t s_neighbor_refs_issued = 0;
    size_t s_neighbor_refs_done = 0;
    size_t s_act_partial_nacks = 0;
    size_t s_bloom_positives = 0;
    size_t s_bloom_false_positives = 0;
    size_t s_forced_precharges = 0;
    size_t s_maintenance_ops = 0;
    size_t s_rows_refreshed = 0;
    size_t s_max_pending = 0;
    uint64_t m_ops_base = 0;
    uint64_t m_rows_base = 0;

    // The chips whose maintenance holds the subarray
    int count_busy_chips(int bank, int sa) const {
        int busy = 0;
        for (const auto& locks : m_locks) {
            busy += locks->under_maintenance(bank, sa);
        }
        return busy;
    }

    // The DRAM answers the oldest due ACT with ACT_NACK.
    void deliver_nacks() {
        while (!m_pending_nacks.empty() && m_pending_nacks.front().due <= m_clk) {
            Request nack(m_pending_nacks.front().addr_vec, Request::Cmd, m_cmd_act_nack);
            nack.command = m_cmd_act_nack;
            m_device.issue_command(nack.command, nack.addr_vec, m_clk);
            for (auto* p : m_plugins) {
                p->on_issue(nack);
            }
            m_pending_nacks.pop_front();
        } 
    }

    // A row left open for too long keeps maintenance out of its subarray (a stream of hits to it can do so,
    // and so can a row nothing accesses anymore): precharge the bank.
    void close_idle_long_open_rows() {
        for (int bank = 0; bank < static_cast<int>(m_device.m_bank_nodes.size()); bank++) {
            DRAMNode* node = m_device.m_bank_nodes[bank];
            if (node->m_state != m_state_opened) {
                m_prepb_injected[bank] = 0;
                continue;
            }
            if (m_prepb_injected[bank] || m_clk - m_act_clk[bank] <= m_force_precharge_after) {
                continue;
            }
            AddrVec_t addr(m_device.m_spec->level_count, -1);
            for (auto* n = node; n; n = n->m_parent_node) {
                addr[n->m_level] = n->m_node_id;
            }
            Request req(std::move(addr), Request::Cmd, m_cmd_prepb);
            if (priority_send(req)) {
                m_prepb_injected[bank] = 1;
                s_forced_precharges++;
            }
        }
    }
};

void SMDController::tick() {
    tick_prologue();

    for (auto& engine : m_engines) {
        engine->tick();
    }
    for (auto& rh : m_rh) {
        rh->tick();
    }

    m_refresh->tick();
    if (m_enabled) {
        close_idle_long_open_rows();
    }

    m_rowpolicy->pre_schedule();
    for (auto* p : m_plugins) {
        p->pre_schedule();
    }

    deliver_nacks();

    Candidate cand = pick_best_ready_from(m_active_buffer, {});

    if (!cand.valid) {
        cand = pick_priority_if();
    }

    if (!cand.valid) {
        cand = pick_rw_if([&](const Request& req) {
            return !(m_device.m_spec->command_meta[req.command].is_opening &&
                     m_active_per_bank[m_device.get_flat_bank_id(req.addr_vec)] > 0);
        });
    }

    if (cand.valid) {
        upgrade_by_row_policy(cand);

        if (!cand.it->is_stat_updated) {
            update_request_stats(cand.it);
        }

        const int command = cand.it->command;
        const int bank = m_device.get_flat_bank_id(cand.it->addr_vec);
        const bool is_act = command == m_cmd_act;
        const int busy_chips = (m_enabled && is_act) ? count_busy_chips(bank, cand.it->addr_vec[m_subarray_level]) : 0;
        const bool nacked = busy_chips > 0;

        m_device.issue_command(command, cand.it->addr_vec, m_clk);
        if (is_act) {
            m_act_clk[bank] = m_clk;
        }
        if (is_act && !m_rh.empty() && busy_chips == 0) {
            const int rank = bank / m_banks_per_rank;
            for (int chip = 0; chip < m_num_chips; chip++) {
                m_rh[chip * m_num_ranks + rank]->on_activation(bank % m_banks_per_rank, cand.it->addr_vec[m_subarray_level], cand.it->addr_vec[m_row_level]);
            }
        }
        if (nacked) {
            m_pending_nacks.push_back({m_clk + m_nACTtoNACK, cand.it->addr_vec});
            (busy_chips == m_num_chips ? s_act_nacks : s_act_partial_nacks)++;
        }

        m_rowpolicy->on_issue(*cand.it);
        for (auto* p : m_plugins) {
            p->on_issue(*cand.it);
        }

        if (command == cand.it->final_command) {
            retire_request(cand.it, *cand.buffer);
        } else if (m_device.m_spec->command_meta[command].is_opening && cand.buffer != &m_active_buffer) {
            promote_to_active(cand.it, *cand.buffer);
        }
    }

    m_rowpolicy->post_schedule();
    for (auto* p : m_plugins) {
        p->post_schedule();
    }
}

} // namespace Ramulator
