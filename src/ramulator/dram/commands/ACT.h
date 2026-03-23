#ifndef RAMULATOR_DRAM_COMMANDS_ACT_H
#define RAMULATOR_DRAM_COMMANDS_ACT_H

#include <stdexcept>

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct ACT {
  static constexpr DRAMCommandMeta meta = {.is_opening = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    bank->m_state = T::State::Opened;
    bank->m_row_state[addr_vec[T::Level::Row]] = T::State::Opened;
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    switch (bank->m_state) {
      case T::State::Closed:
        return T::Command::ACT;
      case T::State::Opened:
        if (bank->m_row_state.find(addr_vec[T::Level::Row]) != bank->m_row_state.end()) {
          return cmd;
        } else {
          return T::Command::PREpb;
        }
      default:
        throw std::runtime_error("[ACT] Invalid bank state!");
    }
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_ACT_H
