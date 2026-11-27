#ifndef RAMULATOR_DRAM_COMMANDS_ACT_NACK_H
#define RAMULATOR_DRAM_COMMANDS_ACT_NACK_H

#include "ramulator/dram/commands/subarray.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

// Self-Managing DRAM: the DRAM rejects an ACT to a subarray under maintenance.
// The ank and its subarrays are closed again, and the subarray accepts an ACT only after nNACK_RESEND cycles.
// Never a request's own command: the controller issues it nACTtoNACK cycles after the NACKed ACT.
template <class T>
struct ACT_NACK {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    PREpbsa<T>::action(bank, cmd, addr_vec, clk);
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return cmd;
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_ACT_NACK_H
