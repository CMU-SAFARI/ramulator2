#ifndef RAMULATOR_DRAM_DEVICE_H
#define RAMULATOR_DRAM_DEVICE_H

#include <memory>
#include <vector>

#include "ramulator/base/config_node.h"
#include "ramulator/base/type.h"
#include "ramulator/dram/dram_spec.h"
#include "ramulator/dram/node.h"

namespace Ramulator {

/**
 * @brief    DRAM Device — owns the DRAMSpec, node tree, and flat bank array.
 *
 * Provides all device-level operations: command issue (timing + state),
 * prerequisite checks, row buffer queries. The controller delegates here
 * for anything that touches DRAM state or timing.
 *
 * All operations take Clk_t clk as a parameter — the device is stateless
 * with respect to simulation time. The controller owns the clock.
 */
class DRAMDevice {
 public:
  std::unique_ptr<DRAMSpec> m_spec_owner;  // Owns the polymorphic spec
  DRAMSpec* m_spec = nullptr;              // Non-owning pointer for convenient access
  std::unique_ptr<DRAMNode> m_root;        // Hierarchical node tree (for timing)
  std::vector<DRAMNode*> m_bank_nodes;     // Flat bank view (non-owning, for state dispatch)
  int m_bank_level = -1;                   // Cached level ID for "Bank" (hot-path use)

  void init(std::unique_ptr<DRAMSpec> spec);
  void set_channel_id(int channel_id);

  // Issue a command: update timing (hierarchical) then apply state (flat bank dispatch)
  void issue_command(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Timing-only check — hierarchical (walks node tree)
  bool check_timing(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Prerequisite check — flat bank dispatch
  int get_preq_command(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Row buffer hit check — flat bank lookup (always single bank)
  bool check_rowbuffer_hit(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Row open check — flat bank lookup (always single bank)
  bool check_node_open(int command, const AddrVec_t& addr_vec, Clk_t clk);

  // Compute flat bank index from addr_vec
  int get_flat_bank_id(const AddrVec_t& addr_vec) const;

  // Check if a bank node matches an addr_vec pattern (wildcards are -1)
  static bool bank_matches(DRAMNode* bank, const AddrVec_t& addr_vec);

  // Get indices into m_bank_nodes for the target banks of a command (cold-path wrapper)
  std::vector<int> get_target_banks(int command, const AddrVec_t& addr_vec) const;

  /// Visit target bank(s) for a command with early-exit support.
  /// Visitor signature: bool(int bank_id) — return true to continue, false to stop.
  /// Returns true if all banks were visited, false if visitor short-circuited.
  template <class Visitor>
  bool for_each_target_bank_while(int command, const AddrVec_t& addr_vec, Visitor&& visitor) const {
    switch (m_spec->bank_targets[command]) {
      case BankTarget::Single:
        return visitor(get_flat_bank_id(addr_vec));

      case BankTarget::All:
        for (int i = 0; i < static_cast<int>(m_bank_nodes.size()); ++i) {
          if (!bank_matches(m_bank_nodes[i], addr_vec)) continue;
          if (!visitor(i)) return false;
        }
        return true;

      case BankTarget::SameBank: {
        const int target_bank_id = addr_vec[m_bank_level];
        for (int i = 0; i < static_cast<int>(m_bank_nodes.size()); ++i) {
          if (m_bank_nodes[i]->m_node_id != target_bank_id) continue;
          if (!bank_matches(m_bank_nodes[i], addr_vec)) continue;
          if (!visitor(i)) return false;
        }
        return true;
      }
    }
    return true;
  }

  /// Visit all target bank(s) for a command. Visitor signature: void(int bank_id).
  template <class Visitor>
  void for_each_target_bank(int command, const AddrVec_t& addr_vec, Visitor&& visitor) const {
    for_each_target_bank_while(command, addr_vec, [&](int bank_id) {
      visitor(bank_id);
      return true;
    });
  }

 private:
  // Flat bank dispatch — apply action to target banks
  void apply_action(int command, const AddrVec_t& addr_vec, Clk_t clk);
};

}  // namespace Ramulator

#endif  // RAMULATOR_DRAM_DEVICE_H
