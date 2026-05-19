#ifndef RAMULATOR_DRAM_COMMANDS_RCKSTRT_H
#define RAMULATOR_DRAM_COMMANDS_RCKSTRT_H

#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

template <class T>
struct RCKSTRT {
  static constexpr DRAMCommandMeta meta = {.is_column_command = true};
  static constexpr BankTarget bank_target = BankTarget::All;
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_RCKSTRT_H
