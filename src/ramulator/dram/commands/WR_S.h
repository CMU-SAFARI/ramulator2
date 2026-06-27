#ifndef RAMULATOR_DRAM_COMMANDS_WR_S_H
#define RAMULATOR_DRAM_COMMANDS_WR_S_H

#include "ramulator/dram/commands/WR.h"

namespace Ramulator::Cmd {

template <class T>
struct WR_S : WR<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_WR_S_H
