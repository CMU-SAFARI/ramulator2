#ifndef RAMULATOR_DRAM_COMMANDS_CAS_H
#define RAMULATOR_DRAM_COMMANDS_CAS_H

#include "ramulator/dram/dram_spec.h"

namespace Ramulator::Cmd {

// LPDDR6 unified CAS WCK2CK sync marker. It carries timing only and does not
// change bank state.
template <class T>
struct CAS {
  static constexpr DRAMCommandMeta meta = {};
  static constexpr BankTarget bank_target = BankTarget::Single;
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_CAS_H
