#ifndef RAMULATOR_DRAM_COMMANDS_PREAB_H
#define RAMULATOR_DRAM_COMMANDS_PREAB_H

#include "ramulator/dram/commands/PREpb.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

// PREab = PREpb::action applied to all banks.
template <class T>
struct PREab {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::All;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    PREpb<T>::action(bank, cmd, addr_vec, clk);
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_PREAB_H
