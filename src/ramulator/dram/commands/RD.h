#ifndef RAMULATOR_DRAM_COMMANDS_RD_H
#define RAMULATOR_DRAM_COMMANDS_RD_H

#include "ramulator/dram/commands/ACT.h"
#include "ramulator/dram/commands/ACT2.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct RD {
  static constexpr DRAMCommandMeta meta = {.is_accessing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    // Compile-time dispatch: ACT2 for split-activate (LPDDR5), ACT otherwise
    if constexpr (requires { T::Command::ACT2; }) {
      return ACT2<T>::preq(bank, cmd, addr_vec, clk);
    } else {
      return ACT<T>::preq(bank, cmd, addr_vec, clk);
    }
  }

  static bool rowhit(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return bank->m_state == T::State::Opened &&
           bank->m_row_state.find(addr_vec[T::Level::Row]) != bank->m_row_state.end();
  }

  static bool rowopen(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return bank->m_state == T::State::Opened;
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_RD_H
