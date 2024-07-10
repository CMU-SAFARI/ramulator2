#ifndef RAMULATOR_DRAM_LAMBDAS_ROWHIT_H
#define RAMULATOR_DRAM_LAMBDAS_ROWHIT_H

#include <spdlog/spdlog.h>

namespace Ramulator {
namespace Lambdas {
namespace RowHit {
namespace Bank {
  template <class T>
  bool RDWR(typename T::Node* node, int cmd, int target_id, Clk_t clk) {
    switch (node->m_state)  {
      case T::m_states["Closed"]: return false;
      case T::m_states["Opened"]:
        if (node->m_row_state.find(target_id) != node->m_row_state.end()) {
          return true;
        }
        else {
          return false;
        }
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

#endif  // RAMULATOR_DRAM_LAMBDAS_ROWHIT_H