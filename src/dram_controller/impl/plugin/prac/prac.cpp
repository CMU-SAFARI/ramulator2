#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "dram_controller/impl/plugin/prac/prac.h"
#include "dram_controller/impl/plugin/device_config/device_config.h"

#include <limits>
#include <vector>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Ramulator {

class PRAC : public IControllerPlugin, public Implementation, public IPRAC {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, PRAC, "PRAC", "PRAC.")

private:
    class PerBankCounters;

private:
    DeviceConfig m_cfg;
    std::vector<PRAC::PerBankCounters> m_bank_counters;
    std::vector<int> m_same_bank_offsets;

    Clk_t m_clk = 0;

    ABOState m_state = ABOState::NORMAL;
    Clk_t m_abo_recovery_start = std::numeric_limits<Clk_t>::max();

    int m_abo_act_ns = -1;
    int m_abo_recovery_refs = -1;
    int m_abo_delay_acts = -1;
    int m_abo_thresh = -1;

    int m_abo_act_cycles = -1;

    uint32_t m_abo_recov_rem_refs = -1;
    uint32_t m_abo_delay_rem_acts = -1;
    bool m_is_abo_needed = false;

    bool m_debug = false;

    uint64_t s_num_recovery = 0;

public:
    void init() override { 
        m_debug = param<bool>("debug").default_val(false);
        m_abo_delay_acts = param<int>("abo_delay_acts").default_val(4);
        m_abo_recovery_refs = param<int>("abo_recovery_refs").default_val(4);
        m_abo_act_ns = param<int>("abo_act_ns").default_val(180);
        m_abo_thresh = param<int>("abo_threshold").default_val(512);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        m_cfg.set_device(cast_parent<IDRAMController>());
        init_dram_params(m_cfg.m_dram);

        m_is_abo_needed = false;
        m_abo_act_cycles = m_abo_act_ns / ((float) m_cfg.m_dram->m_timing_vals("tCK_ps") / 1000.0f);

        m_bank_counters.reserve(m_cfg.m_num_banks);
        for (int i = 0; i < m_cfg.m_num_banks; i++) {
            m_bank_counters.emplace_back(i, m_cfg, m_is_abo_needed, m_abo_thresh, m_debug);
        }

        register_stat(s_num_recovery).name("prac_num_recovery");
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
        m_clk++;

        update_state_machine(request_found, *req_it);

        if (!request_found) {
            return;
        }

        auto& req = *req_it;
        auto& req_meta = m_cfg.m_dram->m_command_meta(req.command);
        auto& req_scope = m_cfg.m_dram->m_command_scopes(req.command);

        bool has_bank_wildcard = req.addr_vec[m_cfg.m_bank_level] == -1;
        bool has_bankgroup_wildcard = req.addr_vec[m_cfg.m_bankgroup_level] == -1;
        if (has_bankgroup_wildcard && has_bank_wildcard) { // All BG, All Bank
            int offset = req.addr_vec[m_cfg.m_rank_level] * m_cfg.m_num_banks_per_rank;
            for (int i = 0; i < m_cfg.m_num_banks_per_rank; i++) {
                m_bank_counters[offset + i].on_request(req);
            }
            req.addr_vec[m_cfg.m_bank_level] = -1;
        }
        else if (has_bankgroup_wildcard) { // All BG, Single Bank
            int rank_offset = req.addr_vec[m_cfg.m_rank_level] * m_cfg.m_num_banks_per_rank;
            int bank_offset = req.addr_vec[m_cfg.m_bank_level];
            for (int i = 0; i < m_cfg.m_num_bankgroups; i++) {
                int bg_offset = i * m_cfg.m_num_banks_per_bankgroup;
                m_bank_counters[rank_offset + bg_offset + bank_offset].on_request(req);
            }
        }
        else if (has_bank_wildcard) { // Single BG, All Bank
            int rank_offset = req.addr_vec[m_cfg.m_rank_level] * m_cfg.m_num_banks_per_rank;
            int bg_offset = req.addr_vec[m_cfg.m_bankgroup_level] * m_cfg.m_num_banks_per_bankgroup; 
            for (int i = 0; i < m_cfg.m_num_banks_per_bankgroup; i++) {
                m_bank_counters[rank_offset + bg_offset + i].on_request(req);
            }
        }
        else { // Single BG, Single Bank
            auto flat_bank_id = m_cfg.get_flat_bank_id(req);
            m_bank_counters[flat_bank_id].on_request(req);
        }
    }

    void update_state_machine(bool request_found, const Request& req) {
        std::unordered_map<ABOState, std::string> state_names = {
            {ABOState::NORMAL, "ABOState::NORMAL"},
            {ABOState::PRE_RECOVERY, "ABOState::PRE_RECOVERY"},
            {ABOState::RECOVERY, "ABOState::RECOVERY"},
            {ABOState::DELAY, "ABOState::DELAY"}
        };
        auto cmd_prea = m_cfg.m_dram->m_commands("PREA");
        auto cmd_rfmab = m_cfg.m_dram->m_commands("RFMab");
        auto cmd_rfmsb = m_cfg.m_dram->m_commands("RFMsb");
        auto cmd_act = m_cfg.m_dram->m_commands("ACT");
        auto cur_state = m_state;
        switch(m_state) {
        case ABOState::NORMAL:
            if (m_is_abo_needed) {
                if (m_debug) {
                    std::printf("[PRAC] [%lu] <%s> Asserting ALERT_N.\n", m_clk, state_names[cur_state].c_str());
                }
                m_state = ABOState::PRE_RECOVERY;
                m_abo_recovery_start = m_clk + m_abo_act_cycles;
                s_num_recovery++;
            }
            break;
        case ABOState::PRE_RECOVERY:
            if (request_found && req.command == cmd_prea) {
                if (m_debug) {
                    std::printf("[PRAC] [%lu] <%s> Received PREA.\n", m_clk, state_names[cur_state].c_str());
                }
            }
            if (m_clk == m_abo_recovery_start) {
                m_state = ABOState::RECOVERY;
                m_abo_recovery_start = std::numeric_limits<Clk_t>::max();
                m_abo_recov_rem_refs = m_abo_recovery_refs * m_cfg.m_num_ranks;
            }
            break;
        case ABOState::RECOVERY:
            if (request_found && (req.command == cmd_rfmab ||
                req.command == cmd_rfmsb)) {
                m_abo_recov_rem_refs--;
                if (!m_abo_recov_rem_refs) {
                    m_state = ABOState::DELAY;
                    m_abo_delay_rem_acts = m_abo_delay_acts;
                }
            }
            break;
        case ABOState::DELAY:
            if (request_found && req.command == cmd_act) {
                m_abo_delay_rem_acts--;
                if (!m_abo_delay_rem_acts) {
                    m_is_abo_needed = false;
                    for (int i = 0; i < m_cfg.m_num_banks; i++) {
                        m_is_abo_needed |= m_bank_counters[i].is_critical();
                    }
                    m_state = ABOState::NORMAL;
                }
            }
            break;
        }
        if (m_debug && cur_state != m_state) {
            std::printf("[PRAC] [%lu] <%s> -> <%s>\n", m_clk, state_names[cur_state].c_str(), state_names[m_state].c_str());
        }
    }

    Clk_t next_recovery_cycle() override {
        return m_abo_recovery_start;
    }

    int get_num_abo_recovery_refs() override {
        return m_abo_recovery_refs;
    }

    ABOState get_state() override {
        return m_state;
    }

private:
    class PerBankCounters {
    public: 
        PerBankCounters(int bank_id, DeviceConfig& cfg, bool& is_abo_needed, int alert_thresh, bool debug)
        : m_bank_id(bank_id), m_cfg(cfg), m_is_abo_needed(is_abo_needed),
        m_alert_thresh(alert_thresh), m_debug(debug) {
            init_dram_params(m_cfg.m_dram);
            reset();
        }

        ~PerBankCounters() {
            m_counters.clear();
        }

        void on_request(const Request& req) {
            if (m_handlertable.find(req.command) != m_handlertable.end()) {
                m_handlertable[req.command].handler(req);
            }
        }

        void init_dram_params(IDRAM* dram) {
            CommandHandler handlers[] = {
                // TODO: We should process PREs? Doesn't really change the results though.
                {std::string("ACT"), std::bind(&PerBankCounters::process_act, this, std::placeholders::_1)},
                {std::string("RFMab"), std::bind(&PerBankCounters::process_rfm, this, std::placeholders::_1)},
                {std::string("RFMsb"), std::bind(&PerBankCounters::process_rfm, this, std::placeholders::_1)}
            };
            for (auto& h : handlers) {
                if (!dram->m_commands.contains(h.cmd_name)) {
                    std::cout << "[PRAC] Command " << h.cmd_name << "does not exist." << std::endl;
                    exit(0);
                }
                m_handlertable[dram->m_commands(h.cmd_name)] = h;
            }
        }

        void reset() {
            m_counters.clear();
            m_critical_rows.clear();
        }

        bool is_critical() {
            return m_critical_rows.size() > 0;
        }

    private:
        struct CommandHandler {
            std::string cmd_name;
            std::function<void(const Request&)> handler;
        };

        DeviceConfig& m_cfg;
        bool& m_is_abo_needed;

        std::unordered_map<int, uint32_t> m_counters;
        std::unordered_map<int, uint32_t> m_critical_rows;
        std::unordered_map<int, CommandHandler> m_handlertable;

        int m_alert_thresh = -1;
        bool m_debug = false;
        int m_bank_id = -1;

        void process_act(const Request& req) {
            auto row_addr = req.addr_vec[m_cfg.m_row_level];    
            if (m_counters.find(row_addr) == m_counters.end()) {
                m_counters[row_addr] = 0;
            }
            m_counters[row_addr]++;
            if (m_debug) {
                std::printf("[PRAC] [%d] [ACT] Row: %d Act: %u\n",
                    m_bank_id, row_addr, m_counters[row_addr]);
            }
            if (m_counters[row_addr] >= m_alert_thresh) {
                m_critical_rows[row_addr] = m_counters[row_addr];
                m_is_abo_needed = true;
            }
        }

        void process_rfm(const Request& req) {
            auto act_max = std::max_element(m_counters.begin(), m_counters.end(),
                [] (const std::pair<int, uint32_t>& p1, const std::pair<int, uint32_t>& p2) {
                    return p1.second < p2.second;
                });
            if (act_max == m_counters.end()) {
                if (m_debug) {
                    std::printf("[PRAC] [%d] [RFM] No critical row.\n", m_bank_id);
                }
                return;
            }
            if (m_debug) {
                std::printf("[PRAC] [%d] [RFM] Row: %d Act: %u\n",
                    m_bank_id, act_max->first, m_counters[act_max->first]);
            }
            m_counters[act_max->first] = 0;
            m_critical_rows.erase(act_max->first);
        }
    };  // class PerBankCounters

};      // class PRAC

}       // namespace Ramulator