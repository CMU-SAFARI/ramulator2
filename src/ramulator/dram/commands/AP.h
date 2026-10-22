#ifndef RAMULATOR_DRAM_COMMANDS_AP_H
#define RAMULATOR_DRAM_COMMANDS_AP_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// SIMDRAM ACTIVATE-PRECHARGE: a single activation used to read back or reset a
// compute row, without the second (copy) activation of an AAP.
//   latency = nRAS + nRP
template <class T>
struct AP : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_AP_H
