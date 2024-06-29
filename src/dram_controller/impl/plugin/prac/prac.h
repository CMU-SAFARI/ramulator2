#ifndef RAMULATOR_PLUGIN_PRAC_H_
#define RAMULATOR_PLUGIN_PRAC_H_

#include "dram/dram.h"

#include <unordered_map>

#define _CYCLES(timing_name)    dram->m_timing_vals(timing_name)
#define _COMMAND(command_name)  dram->m_commands(command_name)

namespace Ramulator {

class IPRAC {
public:
    enum class ABOState {
        NORMAL,
        PRE_RECOVERY,
        RECOVERY,
        DELAY
    };

public:
    virtual Clk_t next_recovery_cycle() = 0;
    virtual int get_num_abo_recovery_refs() = 0;
    virtual ABOState get_state() = 0;

    void init_dram_params(IDRAM* dram) {
        auto write_to_pre_timing = _CYCLES("nCWL") + _CYCLES("nBL") + _CYCLES("nWR");
        read_cycles = _CYCLES("nRAS") + _CYCLES("nRTP") + _CYCLES("nRP");
        write_cycles = _CYCLES("nRAS") + write_to_pre_timing + _CYCLES("nRP");
        cmd_to_min_cycles[_COMMAND("ACT")] = write_cycles; // TODO: Slightly overshooting reads here
        cmd_to_min_cycles[_COMMAND("RD")] = _CYCLES("nRTP") + _CYCLES("nRP");
        cmd_to_min_cycles[_COMMAND("WR")] = write_to_pre_timing + _CYCLES("nRP");
        cmd_to_min_cycles[_COMMAND("RFMsb")] = _CYCLES("nRFMsb");
        cmd_to_min_cycles[_COMMAND("RFMab")] = _CYCLES("nRFM1");
        cmd_to_min_cycles[_COMMAND("REFsb")] = _CYCLES("nRFCsb");
        cmd_to_min_cycles[_COMMAND("REFab")] = _CYCLES("nRFC1");
    }

    // TODO: Get these from m_timing_cons...
    int min_cycles_with_preall(const ReqBuffer::iterator& req) {
        return min_cycles_with_preall(*req);
    }

    int min_cycles_with_preall(const Request& req) {
        if (cmd_to_min_cycles.find(req.command) == cmd_to_min_cycles.end()) {
            return 0;
        }
        return cmd_to_min_cycles[req.command];
    }

private:
    std::unordered_map<int, int> cmd_to_min_cycles;
    int write_cycles = -1;
    int read_cycles = -1;

};      //  class IPRAC

}       //  namespace Ramulator

#endif // RAMULATOR_PLUGIN_PRAC_H_
