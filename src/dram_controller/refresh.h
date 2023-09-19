#ifndef     RAMULATOR_CONTROLLER_REFRESH_H
#define     RAMULATOR_CONTROLLER_REFRESH_H

#include <vector>
#include <string>

#include "base/base.h"


namespace Ramulator {

class IRefreshManager {
  RAMULATOR_REGISTER_INTERFACE(IRefreshManager, "RefreshManager", "Refresh Manager Interface.");

  public:
    virtual void tick() = 0;
};

}        // namespace Ramulator


#endif   // RAMULATOR_CONTROLLER_REFRESH_H