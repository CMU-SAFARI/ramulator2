#ifndef RAMULATOR_DRAM_COMMANDS_WRA_S_H
#define RAMULATOR_DRAM_COMMANDS_WRA_S_H

#include "ramulator/dram/commands/WRA.h"

namespace Ramulator::Cmd {

template <class T>
struct WRA_S : WRA<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_WRA_S_H
