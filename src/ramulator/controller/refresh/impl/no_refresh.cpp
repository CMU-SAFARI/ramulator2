#include "ramulator/controller/refresh/i_refresh_manager.h"

namespace Ramulator {

// No-op refresh — does nothing. Used when refresh is not modeled
class NoRefresh : public IRefreshManager, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IRefreshManager, NoRefresh, "NoRefresh")

  void init() override {
  }
  void tick() override {
  }
};

}  // namespace Ramulator
