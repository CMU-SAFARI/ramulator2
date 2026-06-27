#ifndef RAMULATOR_DRAM_COMMANDS_PREPB_H
#define RAMULATOR_DRAM_COMMANDS_PREPB_H

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct PREpb {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    bank->m_state = T::State::Closed;
    bank->m_row_state.clear();
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    switch (bank->m_state) {
      case T::State::Closed:
        return cmd;
      case T::State::Opened:
        return T::Command::PREpb;
      default:
        return T::Command::PREpb;
    }
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_PREPB_H
