#ifndef RAMULATOR_DRAM_COMMANDS_RFMPB_H
#define RAMULATOR_DRAM_COMMANDS_RFMPB_H

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct RFMpb {
  static constexpr DRAMCommandMeta meta = {.is_refreshing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    if (bank->m_state != T::State::Closed) {
      return T::Command::PREpb;
    }
    return cmd;
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_RFMPB_H
