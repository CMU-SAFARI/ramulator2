#ifndef RAMULATOR_DRAM_COMMANDS_PUM_OP_H
#define RAMULATOR_DRAM_COMMANDS_PUM_OP_H

#include "ramulator/dram/commands/subarray.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

// Shared behaviour of processing-using-DRAM commands: an in-subarray
// activation/precharge sequence that needs the bank closed and leaves it
// closed. The per-command latency lives in the standard's timing constraints.
// Requires a standard with a Subarray level.
template <class T>
struct PumOp {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    PREpbsa<T>::action(bank, cmd, addr_vec, clk);
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    if (bank->m_state == T::State::Closed) {
      return cmd;
    }
    return T::Command::PREpb;
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_PUM_OP_H
