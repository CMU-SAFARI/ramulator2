#ifndef RAMULATOR_CONTROLLER_REFRESH_I_REFRESH_MANAGER_H
#define RAMULATOR_CONTROLLER_REFRESH_I_REFRESH_MANAGER_H

#include "ramulator/base/base.h"

namespace Ramulator {

// Issues periodic refresh commands to maintain DRAM data integrity.
class IRefreshManager {
  RAMULATOR_REGISTER_INTERFACE(IRefreshManager, "refresh_manager")
 public:
  // Called every controller clock cycle to check if a refresh is due.
  virtual void tick() = 0;

  // Whether the refresh schedule forbids `command` to `addr_vec` this cycle although the timing
  // graph allows it, e.g. RFMpb in the pause between two REFpb sets (JESD239D §6.13).
  virtual bool blocks(int command, const AddrVec_t& addr_vec) const {
    return false;
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_REFRESH_I_REFRESH_MANAGER_H
