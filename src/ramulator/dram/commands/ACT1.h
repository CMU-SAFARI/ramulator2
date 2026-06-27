#ifndef RAMULATOR_DRAM_COMMANDS_ACT1_H
#define RAMULATOR_DRAM_COMMANDS_ACT1_H

#include <stdexcept>

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct ACT1 {
  static constexpr DRAMCommandMeta meta = {.is_opening = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    bank->m_state = T::State::Activating;
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    switch (bank->m_state) {
      case T::State::Closed:
        return T::Command::ACT1;
      case T::State::Activating:
        return cmd;
      case T::State::Opened:
        if (bank->m_row_state.find(addr_vec[T::Level::Row]) != bank->m_row_state.end()) {
          return cmd;
        }
        return T::Command::PREpb;
      default:
        throw std::runtime_error("[ACT1] Invalid bank state!");
    }
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_ACT1_H
