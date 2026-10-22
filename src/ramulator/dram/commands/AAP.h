#ifndef RAMULATOR_DRAM_COMMANDS_AAP_H
#define RAMULATOR_DRAM_COMMANDS_AAP_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// SIMDRAM ACTIVATE-ACTIVATE-PRECHARGE: copies a row into the compute rows and
// triggers majority-of-three in the sense amplifiers.  The workhorse of
// bit-serial in-DRAM arithmetic.
//   latency = nRAS + nRP + ceil(nRP/4)
template <class T>
struct AAP : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_AAP_H
