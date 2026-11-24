#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
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
#include "ramulator/controller/smd/i_smd_mechanism.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/*
  Controller for Self-Managing DRAM (SMD): the DRAM maintains itself (refresh, RowHammer protection, ...), one subarray at a time,
  and the controller issues no maintenance commands. An ACT to a subarray under maintenance is rejected:

    cycle t                             the controller issues the act (all ACT timings are charged)
    cycle t + nACTtoNACK                the DRAM answers ACT_NACK; the bank closes again
    cycle t + nACTtoNACK + nNACK_RESEND the subarray accepts an ACT again after nNACK_RESEND (Subarray-level timing);
                                        the request stays at the controller and retries by itself
    
  What the DRAM maintains is up to the smd_mechanisms (ISMDMechanism, controller/smd/impl): each locks the
  regions it works on in lock tables shared by all mechanisms, and this controller turns a lock into a NACK.
  One table is kept per DRAM chip: the chips of a rank refresh on their own, so an ACT may be NACKed by all
  the chips of the rank or by only some of them (a partial NACK; modeld like a full one). Chips run in lockstep
  and are simulated as one unless a mechanism needs them apart.
  Needs a standard with a Subarray level and ACT_NACK (e.g., DDR4_SMD); pair it with
  the NoRefresh refresh manager, since the DRAM refreshes on its own. Without mechanisms nothing is ever locked.
*/
class SMDController : public ControllerBase {
    RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, SMDController, ControllerBase, "SMD")

public:
    void init() override {
        init_base();

        RAMULATOR_CREATE_OPTIONAL_CHILD_LIST(m_mechanisms, ISMDMechanism);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        setup_base(frontend, memory_system);

        auto* spec = m_device.m_spec;
        if (!spec->has_level("Subarray") || !spec->has_command("ACT_NACK")) {
            throw std::runtime_error("SMDController requires a DRAM standard with a Subarray level and the ACT_NACK command (e.g., DDR4_SMD)!");
        }

        m_cmd_act = spec->get_command_id("ACT");
        m_cmd_act_nack = spec->get_command_id("ACT_NACK");
        m_subarray_level = spec->get_level_id("Subarray");
        m_row_level = spec->get_level_id("Row");
        m_nACTtoNACK = spec->get_timing_value("nACTtoNACK");
        m_state_opened = spec->get_state_id("Opened");
        m_cmd_prepb = spec->get_command_id("PREpb");

        const int num_banks = m_device.m_bank_nodes.size();
        m_act_clk.assign(num_banks, 0);
        m_prepb_injected.assign(num_banks, 0);

        // Chips differ only if some mechanism says so
        SMDContext ctx;
        ctx.spec = spec;
        ctx.num_ranks = spec->get_level_size("Rank");
        ctx.banks_per_rank = num_banks / ctx.num_ranks;
        ctx.num_sas = spec->get_level_size("Subarray");
        ctx.rows_per_sa = spec->get_level_size("Row");
        ctx.tCK_ps = m_tCK_ps;
        ctx.chips_per_rank = spec->channel_width / spec->organization.dq;
        const bool chips_differ = std::any_of(m_mechanisms.begin(), m_mechanisms.end(),
                                              [](const ISMDMechanism* m) { return m->needs_chip_simulation(); });
        ctx.num_chips = chips_differ ? ctx.chips_per_rank : 1;
        m_num_chips = ctx.num_chips;

        for (int chip = 0; chip < m_num_chips; chip++) {
            m_locks.push_back(std::make_unique<SMD::LockTable>(num_banks));
            ctx.locks.push_back(m_locks.back().get());
        }
        ctx.is_sa_active = [this](int bank, int sa) {
            const DRAMNode* subarray = m_device.m_bank_nodes[bank]->m_child_nodes[sa].get();
            return subarray->m_state == m_state_opened;
        };

        ctx.is_bank_open = [this](int bank) {
            return m_device.m_bank_nodes[bank]->m_state == m_state_opened;
        };

        m_num_ranks = ctx.num_ranks;
        for (auto* mechanism : m_mechanisms) {
            mechanism->attach(ctx);
            const int64_t limit = mechanism->row_open_limit();
            if (limit > 0) {
                m_row_open_limit = m_row_open_limit > 0 ? std::min(m_row_open_limit, limit) : limit;
            }
        }

        m_stats.add("smd_act_nacks", s_act_nacks);
        m_stats.add("smd_act_partial_nacks", s_act_partial_nacks);
        m_stats.add("smd_forced_precharges", s_forced_precharges);

        for (const char* name : {"maintenance_ops", "rows_refreshed", "max_pending_maintenance", "bloom_positives",
                                 "bloom_false_positives", "neighbor_refs_issued", "neighbor_refs_done",
                                 "scrub_ops", "rows_scrubbed", "max_pending_scrubs", "neighbor_marks_dropped"}) {
            m_smd_stats[name] = 0;
        }
        SMDStatMap reported;
        for (const auto* mechanism : m_mechanisms) {
            mechanism->collect_smd_stats(reported);
        }
        for (const auto& entry : reported) {
            m_smd_stats[entry.first] = 0;
        }
        for (auto& [name, value] : m_smd_stats) {
            m_stats.add("smd_" + name, value);
        }
    }

    void tick() override;

    void update_stats() override {
        ControllerBase::update_stats();
        SMDStatMap reported;
        for (const auto* mechanism : m_mechanisms) {
            mechanism->collect_smd_stats(reported);
        }
        for (auto& [name, value] : m_smd_stats) {
            auto it = reported.find(name);
            value = it == reported.end() ? 0 : it->second;
        }
    }

    void reset_stats() override {
        ControllerBase::reset_stats();
        s_act_nacks = 0;
        s_act_partial_nacks = 0;
        s_forced_precharges = 0;
        for (auto* mechanism : m_mechanisms) {
            mechanism->reset_smd_stats();
        }
        for (auto& entry : m_smd_stats) {
            entry.second = 0;
        }
    }

private:
    struct PendingNack {
        Clk_t due;
        AddrVec_t addr_vec;
    };

    // The maintenance mechanisms
    std::vector<ISMDMechanism*> m_mechanisms;

    // Spec lookups
    int m_cmd_act = -1;
    int m_cmd_act_nack = -1;
    int m_subarray_level = -1;
    int m_row_level = -1;
    int m_state_opened = -1;
    int m_nACTtoNACK = 0;
    int m_cmd_prepb = -1;
    int m_num_ranks = 1;
    int m_num_chips = 1;

    // The regions under maintenance, per simulated chip
    std::vector<std::unique_ptr<SMD::LockTable>> m_locks;
    
    std::deque<PendingNack> m_pending_nacks;
    std::vector<Clk_t> m_act_clk;
    std::vector<char> m_prepb_injected;
    int64_t m_row_open_limit = 0;

    // Stats
    size_t s_act_nacks = 0;
    size_t s_act_partial_nacks = 0;
    size_t s_forced_precharges = 0;
    std::map<std::string, size_t> m_smd_stats;  // reported by the mechanisms, as smd_<name>

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
    void close_long_open_rows() {
        for (int bank = 0; bank < static_cast<int>(m_device.m_bank_nodes.size()); bank++) {
            DRAMNode* node = m_device.m_bank_nodes[bank];
            if (node->m_state != m_state_opened) {
                m_prepb_injected[bank] = 0;
                continue;
            }
            if (m_prepb_injected[bank] || m_clk - m_act_clk[bank] <= m_row_open_limit) {
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

    for (auto* mechanism : m_mechanisms) {
        mechanism->tick();
    }

    m_refresh->tick();
    if (m_row_open_limit > 0) {
        close_long_open_rows();
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
        const int sa = cand.it->addr_vec[m_subarray_level];
        const bool is_act = command == m_cmd_act;
        const int busy_chips = is_act ? count_busy_chips(bank, sa) : 0;

        m_device.issue_command(command, cand.it->addr_vec, m_clk);
        if (is_act) {
            m_act_clk[bank] = m_clk;
        }
        if (is_act && busy_chips == 0) {
            for (auto* mechanism : m_mechanisms) {
                mechanism->on_activation(bank, sa, cand.it->addr_vec[m_row_level]);
            }
        }
        if (busy_chips > 0) {
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
