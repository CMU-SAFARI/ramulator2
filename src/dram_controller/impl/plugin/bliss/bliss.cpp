#include "base/base.h"
#include "dram_controller/controller.h"
#include "dram_controller/plugin.h"
#include "dram_controller/impl/plugin/device_config/device_config.h"
#include "dram_controller/impl/plugin/bliss/bliss.h"
#include "frontend/impl/processor/bhO3/bhllc.h"
#include "frontend/impl/processor/bhO3/bhO3.h"

#include <vector>
#include <algorithm>

namespace Ramulator {

class BLISS : public IControllerPlugin, public Implementation, public IBLISS {
    RAMULATOR_REGISTER_IMPLEMENTATION(IControllerPlugin, BLISS, "BLISS", "BLISS.")

private:
    DeviceConfig m_cfg;
    std::vector<bool> m_blacklist_info;

    int m_prev_src_id = -1;
    int m_consequtive_src_id = -1;

    uint64_t m_clk = 0;

    int m_blacklist_thresh = -1;
    int m_unblacklist_cycles = -1;

    int m_cmd_rd = -1;
    int m_cmd_wr = -1;

    int s_blacklist_count = 0;

public:
    void init() override { 
        m_blacklist_thresh = param<int>("blacklist_thresh").default_val(4);
        m_unblacklist_cycles = param<int>("unblacklist_cycles").default_val(10000);
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
        m_cfg.set_device(cast_parent<IDRAMController>());

        m_blacklist_info.resize(frontend->get_num_cores());

        m_consequtive_src_id = 0;

        m_cmd_rd = m_cfg.m_dram->m_commands("RD");
        m_cmd_wr = m_cfg.m_dram->m_commands("WR");

        register_stat(s_blacklist_count).name("bliss_blacklist_count");
    }

    void update(bool request_found, ReqBuffer::iterator& req_it) override {
        m_clk++;

        if (m_clk % m_unblacklist_cycles == 0) {
            for (int i = 0; i < m_blacklist_info.size(); i++) {
                m_blacklist_info[i] = false;
            }
        }

        if (!request_found) {
            return;
        }

        if (req_it->source_id < 0) {
            return;
        }
        
        // Branchless execution, if equals increment by 1, else set to 0
        // m_consequtive_src_id = (req_it->source_id == m_prev_src_id) * (m_consequtive_src_id + 1);

        m_consequtive_src_id++;
        if (req_it->source_id != m_prev_src_id) {
            m_prev_src_id = req_it->source_id;
            m_consequtive_src_id = 0;
        }

        // Branchless execution, if already blacklisted or exceeding threshold set blacklisted
        // m_blacklist_info[req_it->source_id] = m_blacklist_info[req_it->source_id] || m_consequtive_src_id >= m_blacklist_thresh; 
        // s_blacklist_count += (m_consequtive_src_id >= m_blacklist_thresh);

        if (m_consequtive_src_id >= m_blacklist_thresh) {
            m_blacklist_info[req_it->source_id] = true;
            s_blacklist_count++;
        }
    }

    virtual bool is_blacklisted(int source_id) override {
        return source_id < 0 || m_blacklist_info[source_id];
    }
};      // class BLISS

}       // namespace Ramulator