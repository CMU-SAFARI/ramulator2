#ifndef RAMULATOR_DRAM_COMMANDS_CAS_RD_H
#define RAMULATOR_DRAM_COMMANDS_CAS_RD_H

#include "ramulator/dram/dram_spec.h"

namespace Ramulator::Cmd {

// LPDDR5 CAS WCK2CK sync before read — timing-only marker, no state changes.
template <class T>
struct CAS_RD {
  static constexpr DRAMCommandMeta meta = {};
  static constexpr BankTarget bank_target = BankTarget::Single;
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_CAS_RD_H
