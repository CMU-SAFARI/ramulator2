#ifndef RAMULATOR_DRAM_COMMANDS_RDA_L_H
#define RAMULATOR_DRAM_COMMANDS_RDA_L_H

#include "ramulator/dram/commands/RDA.h"

namespace Ramulator::Cmd {

template <class T>
struct RDA_L : RDA<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_RDA_L_H
