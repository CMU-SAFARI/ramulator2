#ifndef RAMULATOR_DRAM_NODE_H
#define RAMULATOR_DRAM_NODE_H

#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ramulator/base/type.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/**
 * @brief     DRAM Device Node — represents one level in the DRAM hierarchy
 *
 * DRAMNode holds per-node state (m_state, m_row_state, timing history).
 * All spec metadata is accessed through DRAMSpec (runtime, non-templated).
 *
 * State operations (action, preq, rowhit, rowopen) are dispatched by the
 * controller via a flat bank array — only timing uses the hierarchy.
 */
struct DRAMNode {
  DRAMNode* m_parent_node = nullptr;  // Non-owning back-reference
  std::vector<std::unique_ptr<DRAMNode>> m_child_nodes;

  DRAMSpec* m_spec = nullptr;

  int m_level = -1;    // The level of this node in the organization hierarchy
  int m_node_id = -1;  // The id of this node at this level

  int m_state = -1;  // The state of the node

  std::vector<Clk_t> m_cmd_ready_clk;            // The next cycle that each command can be issued again at this level
  std::vector<std::deque<Clk_t>> m_cmd_history;  // Issue-history of each command at this level

  std::unordered_map<int, int> m_row_state;  // The state of the rows, if I am a bank-ish node

  DRAMNode(DRAMSpec* spec, DRAMNode* parent, int level, int id);

  void update_timing(int command, const AddrVec_t& addr_vec, Clk_t clk);
  bool check_timing(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Generic level traversal — visit all descendants at target_level
  template <typename Func>
  void for_each_at_level(int target_level, Func&& fn) {
    if (m_level == target_level) {
      fn(this);
      return;
    }
    for (auto& child : m_child_nodes) {
      child->for_each_at_level(target_level, std::forward<Func>(fn));
    }
  }

  // Scoped traversal — descend to (start_level, start_id), then visit target_level
  template <typename Func>
  void for_each_at_level(int start_level, int start_id, int target_level, Func&& fn) {
    if (m_level == start_level) {
      if (m_node_id == start_id) {
        for_each_at_level(target_level, std::forward<Func>(fn));
      }
      return;
    }
    for (auto& child : m_child_nodes) {
      child->for_each_at_level(start_level, start_id, target_level, std::forward<Func>(fn));
    }
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_NODE_H
