#ifndef RAMULATOR_DRAM_COMMANDS_RD_L_H
#define RAMULATOR_DRAM_COMMANDS_RD_L_H

#include "ramulator/dram/commands/RD.h"

namespace Ramulator::Cmd {

template <class T>
struct RD_L : RD<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_RD_L_H
