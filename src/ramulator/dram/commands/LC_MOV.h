#ifndef RAMULATOR_DRAM_COMMANDS_LC_MOV_H
#define RAMULATOR_DRAM_COMMANDS_LC_MOV_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// MIMDRAM local-column move: transfers an operand within a mat via the help
// flip-flops and column-select logic, used for the intra-mat stage of an
// in-DRAM reduction tree (MIMDRAM, arXiv:2402.19080 section 4.1).
//   latency = 2*(nRAS + nRP) + nRELOC + nWR
template <class T>
struct LC_MOV : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_LC_MOV_H
