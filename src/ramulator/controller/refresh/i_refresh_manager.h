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
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_REFRESH_I_REFRESH_MANAGER_H
