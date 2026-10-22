#ifndef RAMULATOR_DRAM_COMMANDS_GB_MOV_H
#define RAMULATOR_DRAM_COMMANDS_GB_MOV_H

#include "ramulator/dram/commands/PUM_OP.h"

namespace Ramulator::Cmd {

// MIMDRAM global-buffer move: transfers an operand between mats through the
// global row buffer, used to walk the inter-mat stage of an in-DRAM reduction
// tree (MIMDRAM, arXiv:2402.19080 section 4.1).
//   latency = nRAS + nRELOC + nWR + nRP
template <class T>
struct GB_MOV : PumOp<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_GB_MOV_H
