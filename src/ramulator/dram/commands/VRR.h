#ifndef RAMULATOR_DRAM_COMMANDS_VRR_H
#define RAMULATOR_DRAM_COMMANDS_VRR_H

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

// Victim Row Refresh: bank-scoped refresh that refreshes neighboring rows of a recently-activated row
template <class T>
struct VRR {
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

#endif  // RAMULATOR_DRAM_COMMANDS_VRR_H
