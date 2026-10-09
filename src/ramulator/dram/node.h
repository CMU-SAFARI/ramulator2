#ifndef RAMULATOR_DRAM_NODE_H
#define RAMULATOR_DRAM_NODE_H

#include <deque>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "ramulator/base/type.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

/**
 * @brief     Feature-owned state attached to a DRAM node (e.g., a power model's per-bank counters).
 *
 * A feature reserves a typed slot with DRAMDevice::register_node_extension<T>() and attaches its
 * objects to nodes through it. Slots keep features that observe the device from colliding.
 */
struct NodeExtension {
  virtual ~NodeExtension() = default;
};

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
  std::vector<std::deque<Clk_t>> m_shared_window_history;  // Shared rolling-window histories

  std::unordered_map<int, int> m_row_state;  // The state of the rows, if I am a bank-ish node

  std::vector<std::unique_ptr<NodeExtension>> m_ext;  // Feature-owned state, indexed by extension slot

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

/// A slot for one feature's state of type T on DRAM nodes (see DRAMDevice::register_node_extension).
template <typename T>
class NodeExtensionSlot {
  static_assert(std::is_base_of_v<NodeExtension, T>, "T must derive from NodeExtension");

 public:
  NodeExtensionSlot() = default;
  explicit NodeExtensionSlot(int index) : m_index(index) {
  }

  // Create this slot's T on the node.
  T& attach(DRAMNode* node) const {
    if (static_cast<int>(node->m_ext.size()) <= m_index) {
      node->m_ext.resize(m_index + 1);
    }
    node->m_ext[m_index] = std::make_unique<T>();
    return get(node);
  }

  // The node's T (the node must have been attached).
  T& get(DRAMNode* node) const {
    return static_cast<T&>(*node->m_ext[m_index]);
  }

 private:
  int m_index = -1;
};

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_NODE_H
