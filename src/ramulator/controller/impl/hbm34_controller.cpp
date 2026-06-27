#include "ramulator/controller/impl/hbm_controller_base.h"

#include "ramulator/base/base.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

class HBM34Controller final : public HBMControllerBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IController, HBM34Controller, HBMControllerBase, "HBM34")

 private:
  // Tracks the last rising-edge row command until its pairing falling edge.
  // At that edge, falling PRE legality depends on command, PC, and bank.
  struct RisingEdgeCommandInfo {
    int command = -1;                      // Rising-edge row command
    int pc = -1;                           // Pseudo-channel of the rising command
    int bank_key = -1;                     // Per-PC flat (SID, BankGroup, Bank), or -1 for all-bank row commands
    Clk_t next_pairing_falling_edge = -1;  // Edge where pairing rules apply
  };

  int m_cmd_act = -1;
  int m_cmd_prepb = -1;
  int m_cmd_preab = -1;
  int m_cmd_refab = -1;
  int m_cmd_rfmab = -1;
  int m_level_pc = -1;
  int m_level_sid = -1;
  int m_level_bg = -1;
  int m_level_bank = -1;
  int m_bank_groups = -1;
  int m_banks_per_group = -1;

  RisingEdgeCommandInfo m_rising_edge_cmd_info;

 public:
  void init() override {
    HBMControllerBase::init();
    auto& spec = *m_device.m_spec;

    m_cmd_act = spec.get_command_id("ACT");
    m_cmd_prepb = spec.get_command_id("PREpb");
    m_cmd_preab = spec.get_command_id("PREab");
    m_cmd_refab = spec.get_command_id("REFab");
    m_cmd_rfmab = spec.has_command("RFMab") ? spec.get_command_id("RFMab") : -1;

    m_level_pc = spec.get_level_id("PseudoChannel");
    m_level_sid = spec.get_level_id("Sid");
    m_level_bg = spec.get_level_id("BankGroup");
    m_level_bank = spec.get_level_id("Bank");
    m_bank_groups = spec.get_level_size("BankGroup");
    m_banks_per_group = spec.get_level_size("Bank");
  }

  void tick() override {
    hbm_tick_prologue();

    bool rising_edge = is_rising_edge();
    if (rising_edge) {
      // Column commands can only be issued on the rising edge
      try_issue_slot(SlotType::ColumnBus);
    }

    // slot_matches() filter command is overloaded by HBM34Controller to apply
    // clock-edge and command pairing constraints
    if (auto issued = try_issue_slot(SlotType::RowBus)) {
      if (rising_edge) {
        record_rising_row_issued_command_info(issued->command, issued->addr_vec, issued->clk);
      }
    }

    hbm_tick_epilogue();
  }

 protected:
  /**
   * Overloaded slot matching logic that checks 1) the current clock edge, and 2) the rising-edge state
   * to filter commands from the request buffers based on the clock edge and command pairing constraints
   * for HBM3/4.
   */
  bool slot_matches(const Request& req, SlotType slot) const override {
    // Rising edge: use normal HBM slot matching.
    if (is_rising_edge()) {
      return HBMControllerBase::slot_matches(req, slot);
    }

    // Falling edge:
    // Case 1: Column commands cannot issue.
    if (slot == SlotType::ColumnBus) {
      return false;
    }

    // Case 2: Non-PRE commands cannot be issued on the falling edge.
    if ((req.command != m_cmd_prepb) && (req.command != m_cmd_preab)) {
      return false;
    }

    // Case 3: PRE commands need to consult the rising-edge state for command pairing constraints.
    return can_issue_falling_edge_pre(req);
  }

 private:
  inline bool is_rising_edge() const {
    return (m_clk % 2) == 1;
  }

  bool can_issue_falling_edge_pre(const Request& cand) const {
    // Check if we are currently constrained by the pairing rules
    if (m_clk != m_rising_edge_cmd_info.next_pairing_falling_edge) {
      return true;
    }

    // Different PC: Both PREab and PREpb can issue
    if (m_rising_edge_cmd_info.pc != cand.addr_vec[m_level_pc]) {
      return true;
    }

    // Same PC: No PRE can issue if the rising edge command is all-bank
    if (is_all_bank_row_command(m_rising_edge_cmd_info.command)) {
      return false;
    }

    // Same PC: Only PREpb can issue if it targets a different bank than the rising edge command
    if ((cand.command == m_cmd_prepb) && (bank_key(cand.addr_vec) != m_rising_edge_cmd_info.bank_key)) {
      return true;
    }

    return false;
  }

  void record_rising_row_issued_command_info(int command, const AddrVec_t& addr_vec, Clk_t clk) {
    m_rising_edge_cmd_info.command = command;
    m_rising_edge_cmd_info.pc = addr_vec[m_level_pc];
    m_rising_edge_cmd_info.bank_key = is_all_bank_row_command(command) ? -1 : bank_key(addr_vec);
    m_rising_edge_cmd_info.next_pairing_falling_edge = clk + (command == m_cmd_act ? 3 : 1);
  }

  inline bool is_all_bank_row_command(int command) const {
    return command == m_cmd_preab || command == m_cmd_refab || command == m_cmd_rfmab;
  }

  inline int bank_key(const AddrVec_t& addr_vec) const {
    return (addr_vec[m_level_sid] * m_bank_groups + addr_vec[m_level_bg]) * m_banks_per_group + addr_vec[m_level_bank];
  }
};

}  // namespace Ramulator
