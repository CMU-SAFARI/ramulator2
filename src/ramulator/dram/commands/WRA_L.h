#ifndef RAMULATOR_DRAM_COMMANDS_WRA_L_H
#define RAMULATOR_DRAM_COMMANDS_WRA_L_H

#include "ramulator/dram/commands/WRA.h"

namespace Ramulator::Cmd {

template <class T>
struct WRA_L : WRA<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_WRA_L_H
