#ifndef RAMULATOR_DRAM_COMMANDS_WR_L_H
#define RAMULATOR_DRAM_COMMANDS_WR_L_H

#include "ramulator/dram/commands/WR.h"

namespace Ramulator::Cmd {

template <class T>
struct WR_L : WR<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_WR_L_H
