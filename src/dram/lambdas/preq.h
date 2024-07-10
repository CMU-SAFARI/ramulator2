#ifndef RAMULATOR_DRAM_LAMBDAS_PREQ_H
#define RAMULATOR_DRAM_LAMBDAS_PREQ_H

#include <spdlog/spdlog.h>

namespace Ramulator {
namespace Lambdas {
namespace Preq {
namespace Bank {
template <class T>
int RequireRowOpen(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
  switch (node->m_state) {
    case T::m_states["Closed"]: return T::m_commands["ACT"];
    case T::m_states["Opened"]: {
      if (node->m_row_state.find(addr_vec[T::m_levels["row"]]) != node->m_row_state.end()) {
        return cmd;
      } else {
        return T::m_commands["PRE"];
      }
    }    
    case T::m_states["Refreshing"]: return T::m_commands["ACT"];
    default: {
      spdlog::error("[Preq::Bank] Invalid bank state for an RD/WR command!");
      std::exit(-1);      
    } 
  }
};

template <class T>
int RequireBankClosed(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
  switch (node->m_state) {
    case T::m_states["Closed"]: return cmd;
    case T::m_states["Opened"]: return T::m_commands["PRE"];
    case T::m_states["Refreshing"]: return cmd;
    default: {
      spdlog::error("[Preq::Bank] Invalid bank state for an RD/WR command!");
      std::exit(-1);      
    } 
  }
};  
}       // namespace Bank

namespace Rank {
template <class T>
int RequireAllBanksClosed(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
  if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 1) {
    for (auto bank: node->m_child_nodes) {
      if (bank->m_state == T::m_states["Closed"]) {
        continue;
      } else if(bank->m_state == T::m_states["Refreshing"]) {
        return cmd; 
      } else {
        return T::m_commands["PREA"];
      }
    }
  } else if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 2) {
    for (auto bg : node->m_child_nodes) {
      for (auto bank: bg->m_child_nodes) {
        if (bank->m_state == T::m_states["Closed"]) {
          continue;
        } else if(bank->m_state == T::m_states["Refreshing"]) {
          return cmd; 
        } else {
          return T::m_commands["PREA"];
        }
      }
    }
  }
  return cmd;
};

template <class T>
int RequireSameBanksClosed(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
  bool all_banks_ready = true;
  for (auto bg : node->m_child_nodes) {
    for (auto bank : bg->m_child_nodes) {
      if (bank->m_node_id == addr_vec[T::m_levels["bank"]]) {
        all_banks_ready &= (bank->m_state == T::m_states["Closed"]) || (bank->m_state == T::m_states["Refreshing"]);
      }
    }
  }
  if (all_banks_ready) {
    return cmd;
  } else {
    return T::m_commands["PREsb"];
  }
};
}       // namespace Rank
namespace Channel {
  template <class T>
  int RequireAllBanksClosed(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    if constexpr (T::m_levels["bank"] - T::m_levels["channel"] == 2) {
      for (auto bg : node->m_child_nodes) {
        for (auto bank: bg->m_child_nodes) {
          if (bank->m_state == T::m_states["Closed"]) {
            continue;
          } else {
            return T::m_commands["PREA"];
          }
        }
      }
    } else if constexpr (T::m_levels["bank"] - T::m_levels["channel"] == 3) {
      for (auto pc : node->m_child_nodes) {
        for (auto bg : pc->m_child_nodes) {
          for (auto bank: bg->m_child_nodes) {
            if (bank->m_state == T::m_states["Closed"]) {
              continue;
            } else {
              return T::m_commands["PREA"];
            }
          }
        }
      }
    }
    return cmd;
  };
}       // namespace Channel
}       // namespace Preq
}       // namespace Lambdas
};      // namespace Ramulator

#endif  // RAMULATOR_DRAM_LAMBDAS_PREQ_H