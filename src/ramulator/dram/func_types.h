#ifndef RAMULATOR_DRAM_FUNC_TYPES_H
#define RAMULATOR_DRAM_FUNC_TYPES_H

#include <vector>

#include "ramulator/base/type.h"

namespace Ramulator {

// Forward declaration — full definition in node.h
struct DRAMNode;

// Bank-level handler function pointer types.
// All handlers receive a bank-level DRAMNode — the controller dispatches to
// the correct bank(s) via a flat bank array, so the hierarchy is not involved.
using ActionFunc_t = void (*)(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk);
using PreqFunc_t = int (*)(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk);
using RowhitFunc_t = bool (*)(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk);
using RowopenFunc_t = bool (*)(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk);

// 1D handler vectors indexed by command ID (not by level — handlers are always bank-level).
struct FuncArrays {
  std::vector<ActionFunc_t> actions;
  std::vector<PreqFunc_t> preqs;
  std::vector<RowhitFunc_t> rowhits;
  std::vector<RowopenFunc_t> rowopens;

  void resize(int num_cmds) {
    actions.resize(num_cmds, nullptr);
    preqs.resize(num_cmds, nullptr);
    rowhits.resize(num_cmds, nullptr);
    rowopens.resize(num_cmds, nullptr);
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_FUNC_TYPES_H
