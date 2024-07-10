#ifndef RAMULATOR_DRAM_LAMBDAS_ROWOPEN_H
#define RAMULATOR_DRAM_LAMBDAS_ROWOPEN_H

#include <spdlog/spdlog.h>

namespace Ramulator {
namespace Lambdas {
namespace RowOpen {
namespace Bank {
  template <class T>
  bool RDWR(typename T::Node* node, int cmd, int target_id, Clk_t clk) {
    switch (node->m_state)  {
      case T::m_states["Closed"]: return false;
      case T::m_states["Opened"]: return true;
      case T::m_states["Refreshing"]: return false;
      default: {
        spdlog::error("[RowHit::Bank] Invalid bank state for an RD/WR command!");
        std::exit(-1);      
      }
    }
  }
}       // namespace Bank
}       // namespace RowHit
}       // namespace Lambdas
};      // namespace Ramulator

#endif  // RAMULATOR_DRAM_LAMBDAS_ROWOPEN_H