#ifndef RAMULATOR_DRAM_COMMANDS_WR_H
#define RAMULATOR_DRAM_COMMANDS_WR_H

#include "ramulator/dram/commands/ACT.h"
#include "ramulator/dram/commands/RD.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct WR {
  static constexpr DRAMCommandMeta meta = {.is_accessing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RD<T>::preq(bank, cmd, addr_vec, clk);
  }

  static bool rowhit(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RD<T>::rowhit(bank, cmd, addr_vec, clk);
  }

  static bool rowopen(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RD<T>::rowopen(bank, cmd, addr_vec, clk);
  }
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_WR_H
