#ifndef RAMULATOR_DRAM_COMMANDS_PNOT_H
#define RAMULATOR_DRAM_COMMANDS_PNOT_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// PRADA row inversion.
//   latency = nRAS + nRP
template <class T>
struct PNOT : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_PNOT_H
