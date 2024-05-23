#ifndef RAMULATOR_PLUGUTIL_DEVICECFG_H
#define RAMULATOR_PLUGUTIL_DEVICECFG_H

#include "base/base.h"
#include "dram_controller/controller.h"

namespace Ramulator {

class DeviceConfig {
public: 
    int m_channel_level;
    int m_rank_level;
    int m_bank_level;
    int m_bankgroup_level;
    int m_row_level;
    int m_col_level;

    int m_num_ranks;
    int m_num_bankgroups;
    int m_num_banks_per_bankgroup;
    int m_num_banks_per_rank;
    int m_num_rows_per_bank;
    int m_num_cls;
    int m_num_banks;

    IDRAMController* m_ctrl;
    IDRAM* m_dram;

    DeviceConfig();
    DeviceConfig(IDRAMController* ctrl);
    ~DeviceConfig();
    
    void set_device(IDRAMController* ctrl);
    int get_flat_bank_id(const Request& req);
};

}

#endif  // RAMULATOR_PLUGUTIL_DEVICECFG_H 
