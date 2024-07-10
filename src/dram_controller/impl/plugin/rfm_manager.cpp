#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"

namespace Ramulator {

class RFMManager : public IControllerPlugin, public Implementation {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, RFMManager, "RFMManager", "RFM Manager.")

private:
    IDRAM* m_dram = nullptr;
    std::vector<int> m_bank_ctrs;

    Clk_t m_clk = 0;

    int m_rfm_req_id = -1;
    int m_no_send = -1;

    int m_rank_level = -1;
    int m_bank_level = -1;
    int m_bankgroup_level = -1;
    int m_row_level = -1;
    int m_col_level = -1;

    int m_num_ranks = -1;
    int m_num_bankgroups = -1;
    int m_num_banks_per_bankgroup = -1;
    int m_num_banks_per_rank = -1;
    int m_num_rows_per_bank = -1;
    int m_num_cls = -1;

    int m_rfm_thresh = -1;
    bool m_debug = false;

    int s_rfm_counter = 0;

public:
    void init() override { 
        m_rfm_thresh = param<int>("rfm_thresh").default_val(80);
        m_debug = param<bool>("debug").default_val(false);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        m_ctrl = cast_parent<IDRAMController>();
        m_dram = m_ctrl->m_dram;
        if (!m_dram->m_requests.contains("rfm")) {
            std::cout << "[Ramulator::RFMManager] [CRITICAL ERROR] DRAM Device does not support request: rfm" << std::endl; 
            exit(0);
        }
        m_rfm_req_id = m_dram->m_requests("rfm");

        m_rank_level = m_dram->m_levels("rank");
        m_bank_level = m_dram->m_levels("bank");
        m_bankgroup_level = m_dram->m_levels("bankgroup");
        m_row_level = m_dram->m_levels("row");
        m_col_level = m_dram->m_levels("column");

        m_num_ranks = m_dram->get_level_size("rank");
        m_num_bankgroups = m_dram->get_level_size("bankgroup");
        m_num_banks_per_bankgroup = m_dram->get_level_size("bankgroup") < 0 ? 0 : m_dram->get_level_size("bank");
        m_num_banks_per_rank = m_dram->get_level_size("bankgroup") < 0 ? 
                                m_dram->get_level_size("bank") : 
                                m_dram->get_level_size("bankgroup") * m_dram->get_level_size("bank");
        m_num_rows_per_bank = m_dram->get_level_size("row");
        m_num_cls = m_dram->get_level_size("column") / 8;
        
        m_bank_ctrs.resize(m_num_ranks * m_num_banks_per_rank);
        for (int i = 0; i < m_bank_ctrs.size(); i++) {
            m_bank_ctrs[i] = 0;
        }
        m_no_send = 0;

        register_stat(s_rfm_counter).name("rfm_counter");
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
        m_clk++;

        if (!request_found) {
            return;
        }

        auto& req = *req_it;
        auto& req_meta = m_dram->m_command_meta(req.command);
        auto& req_scope = m_dram->m_command_scopes(req.command);
        if (!(req_meta.is_opening && req_scope == m_row_level)) {
            return; 
        }

        int flat_bank_id = req_it->addr_vec[m_bank_level];
        int accumulated_dimension = 1;
        for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
            accumulated_dimension *= m_dram->m_organization.count[i + 1];
            flat_bank_id += req_it->addr_vec[i] * accumulated_dimension;
        }

        m_bank_ctrs[flat_bank_id]++;

        if (m_debug) {
            std::cout << "Rank     : " << req_it->addr_vec[m_rank_level] << std::endl;
            std::cout << "Bank     : " << req_it->addr_vec[m_bank_level] << std::endl;
            std::cout << "BankGroup: " << req_it->addr_vec[m_bankgroup_level] << std::endl;
            std::cout << "Flat Bank: " << flat_bank_id << std::endl;
        }

        if (m_bank_ctrs[flat_bank_id] < m_rfm_thresh) {
            return;
        }
         
        for (int i = 0; i < m_bank_ctrs.size(); i++) {
            m_bank_ctrs[i] = 0;
        }

        Request rfm(req.addr_vec, m_rfm_req_id);
        rfm.addr_vec[m_bankgroup_level] = -1;
        rfm.addr_vec[m_bank_level] = -1;
        // TODO: Add a buffer to retry later
        if (!m_ctrl->priority_send(rfm)) {
            std::cout << "[Ramulator::RFMManager] [CRITICAL ERROR] Could not send request: rfm" << std::endl; 
            exit(0);
        }
        s_rfm_counter++;
    }
};

}       // namespace Ramulator
