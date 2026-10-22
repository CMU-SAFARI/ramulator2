#ifndef RAMULATOR_DRAM_COMMANDS_SUBARRAY_H
#define RAMULATOR_DRAM_COMMANDS_SUBARRAY_H

#include <stdexcept>

#include "ramulator/dram/commands/PREpb.h"
#include "ramulator/dram/commands/RD.h"
#include "ramulator/dram/commands/WR.h"
#include "ramulator/dram/node.h"

namespace Ramulator::Cmd {

// ACT/PRE/RD/WR for standards with a Subarray level between Bank and Row.
//
// Open-row state is kept on the subarray node, since addr_vec[Row] is the row
// within a subarray. A bank still has one open row: activating a row in another
// subarray needs a precharge first.
template <class T>
inline DRAMNode* subarray_of(DRAMNode* bank, const AddrVec_t& addr_vec) {
  return bank->m_child_nodes[addr_vec[T::Level::Subarray]].get();
}

template <class T>
struct ACTsa {
  static constexpr DRAMCommandMeta meta = {.is_opening = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    DRAMNode* subarray = subarray_of<T>(bank, addr_vec);
    bank->m_state = T::State::Opened;
    subarray->m_state = T::State::Opened;
    subarray->m_row_state[addr_vec[T::Level::Row]] = T::State::Opened;
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    switch (bank->m_state) {
      case T::State::Closed:
        return T::Command::ACT;
      case T::State::Opened: {
        DRAMNode* subarray = subarray_of<T>(bank, addr_vec);
        const auto& rows = subarray->m_row_state;
        if (subarray->m_state == T::State::Opened && rows.find(addr_vec[T::Level::Row]) != rows.end()) {
          return cmd;
        }
        return T::Command::PREpb;
      }
      default:
        throw std::runtime_error("[ACTsa] Invalid bank state!");
    }
  }
};

template <class T>
struct PREpbsa {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  // Closes every subarray of the bank.
  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    bank->m_state = T::State::Closed;
    bank->m_row_state.clear();
    for (auto& subarray : bank->m_child_nodes) {
      subarray->m_state = T::State::Closed;
      subarray->m_row_state.clear();
    }
  }

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return bank->m_state == T::State::Closed ? cmd : T::Command::PREpb;
  }
};

template <class T>
struct PREabsa {
  static constexpr DRAMCommandMeta meta = {.is_closing = true};
  static constexpr BankTarget bank_target = BankTarget::All;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    PREpbsa<T>::action(bank, cmd, addr_vec, clk);
  }
};

template <class T>
struct RDsa {
  static constexpr DRAMCommandMeta meta = {.is_accessing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return ACTsa<T>::preq(bank, cmd, addr_vec, clk);
  }

  static bool rowhit(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    if (bank->m_state != T::State::Opened) {
      return false;
    }
    DRAMNode* subarray = subarray_of<T>(bank, addr_vec);
    const auto& rows = subarray->m_row_state;
    return subarray->m_state == T::State::Opened && rows.find(addr_vec[T::Level::Row]) != rows.end();
  }

  static bool rowopen(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return bank->m_state == T::State::Opened;
  }
};

template <class T>
struct WRsa : RDsa<T> {};

// Auto-precharge variants: access, then close the bank.
template <class T>
struct RDAsa {
  static constexpr DRAMCommandMeta meta = {.is_closing = true, .is_accessing = true};
  static constexpr BankTarget bank_target = BankTarget::Single;

  static void action(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    PREpbsa<T>::action(bank, cmd, addr_vec, clk);
  }
  static int preq(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RDsa<T>::preq(bank, cmd, addr_vec, clk);
  }
  static bool rowhit(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RDsa<T>::rowhit(bank, cmd, addr_vec, clk);
  }
  static bool rowopen(DRAMNode* bank, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    return RDsa<T>::rowopen(bank, cmd, addr_vec, clk);
  }
};

template <class T>
struct WRAsa : RDAsa<T> {};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_SUBARRAY_H
