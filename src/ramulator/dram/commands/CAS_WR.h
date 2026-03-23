#ifndef RAMULATOR_DRAM_COMMANDS_CAS_WR_H
#define RAMULATOR_DRAM_COMMANDS_CAS_WR_H

#include "ramulator/dram/dram_spec.h"

namespace Ramulator::Cmd {

// LPDDR5 CAS WCK2CK sync before write — timing-only marker, no state changes.
template <class T>
struct CAS_WR {
  static constexpr DRAMCommandMeta meta = {};
  static constexpr BankTarget bank_target = BankTarget::Single;
};

}  // namespace Ramulator::Cmd

#endif  // RAMULATOR_DRAM_COMMANDS_CAS_WR_H
