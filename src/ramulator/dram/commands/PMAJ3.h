#ifndef RAMULATOR_DRAM_COMMANDS_PMAJ3_H
#define RAMULATOR_DRAM_COMMANDS_PMAJ3_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// PRADA three-input majority via sequential row activation (Shin et al.,
// ICCAD24, doi:10.1145/3676536.3676771).  PRADA computes MAJ3 directly rather
// than composing it from AAP copies, so it needs no compute-row staging.
//   latency = nRAS + nRP + ceil(nRP/4)
template <class T>
struct PMAJ3 : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_PMAJ3_H
