#ifndef RAMULATOR_DRAM_LAMBDAS_POWER_H
#define RAMULATOR_DRAM_LAMBDAS_POWER_H

#include <spdlog/spdlog.h>

namespace Ramulator {
namespace Lambdas {
namespace Power {
namespace Bank {
  template <class T>
  int get_flat_rank_id(typename T::Node* node) {
    int channel_id = -1;
    int rank_id = -1;
    int num_ranks = node->m_spec->get_level_size("rank");
    if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 1) {
      auto rank_node = node->m_parent_node;
      auto channel_node = rank_node->m_parent_node;
      rank_id = rank_node->m_node_id;
      channel_id = channel_node->m_node_id;
    } else if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 2) {
      auto bg_node = node->m_parent_node;
      auto rank_node = bg_node->m_parent_node;
      auto channel_node = rank_node->m_parent_node;
      rank_id = rank_node->m_node_id;
      channel_id = channel_node->m_node_id;
    }
    return channel_id * num_ranks + rank_id;
  }

  template <class T>
  void debug(typename T::Node* node, std::string msg, Clk_t clk) {
    if (node->m_spec->m_power_debug) {
      std::cout << "[Power] Rank" << Bank::get_flat_rank_id<T>(node) << " Bank" << node->m_node_id << " " << msg << " @ " << clk << std::endl;
    }
  }

  template <class T>
  void ACT(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing ACT counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("ACT")]++;
  }

  template <class T>
  void PRE(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing PRE counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("PRE")]++;
  }

  template <class T>
  void RD(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing RD counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("RD")]++;
  }

  template <class T>
  void WR(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing WR counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("WR")]++;
  }

  template <class T>
  void VRR(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing VRR counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("VRR")]++;
  }

  template <class T>
  void RVRR(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Bank::debug<T>(node, "Incrementing RVRR counter.", clk);
    node->m_spec->m_power_stats[Bank::get_flat_rank_id<T>(node)].cmd_counters[T::m_cmds_counted("RVRR")]++;
  }
}      // namespace Bank






namespace Rank {
  template <class T>
  int get_flat_rank_id(typename T::Node* node) {
    int num_ranks = node->m_spec->get_level_size("rank");
    auto channel_node = node->m_parent_node;
    int rank_id = node->m_node_id;
    int channel_id = channel_node->m_node_id;
    return channel_id * num_ranks + rank_id;
  }

  template <class T>
  void debug(typename T::Node* node, std::string msg, Clk_t clk) {
    if (node->m_spec->m_power_debug) {
      std::cout << "[Power] Rank" << Rank::get_flat_rank_id<T>(node) << " " << msg << " @ " << clk << std::endl;
    }
  }

  template <class T>
  int get_open_bank_count(typename T::Node* node) {
    int bank_count = 0;
    if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 1) {
      for (auto bank: node->m_child_nodes) {
        if (bank->m_state == T::m_states["Opened"]) {
          bank_count++;
        }
      }
    } else if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 2) {
      for (auto bg : node->m_child_nodes) {
        for (auto bank: bg->m_child_nodes) {
          if (bank->m_state == T::m_states["Opened"]) {
              bank_count++;
          }
        }
      }
    }
    return bank_count;
  }

  template <class T>
  int get_refreshing_bank_count(typename T::Node* node) {
    int bank_count = 0;
    if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 1) {
      for (auto bank: node->m_child_nodes) {
        if (bank->m_state == T::m_states["Refreshing"]) {
          bank_count++;
        }
      }
    } else if constexpr (T::m_levels["bank"] - T::m_levels["rank"] == 2) {
      for (auto bg : node->m_child_nodes) {
        for (auto bank: bg->m_child_nodes) {
          if (bank->m_state == T::m_states["Refreshing"]) {
              bank_count++;
          }
        }
      }
    }
    return bank_count;
  }

  template <class T>
  void ACT(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------ACT------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 0;
    
    if (is_rank_idle) {
      cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
      cur_power_stats.active_start_cycle = clk;
      std::string msg = "Rank is idle. idle_cycles: " + std::to_string(cur_power_stats.idle_cycles) + "    active_start_cycle: " + std::to_string(cur_power_stats.active_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::ACTIVE;
    }
  }

  template <class T>
  void PRE(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------PRE------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_going_idle = get_open_bank_count<T>(node) == 1 && get_refreshing_bank_count<T>(node) == 0; // TODO: AND this PRE is targetting the active bank

    if (is_rank_going_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is going idle. active_cycles: " + std::to_string(cur_power_stats.active_cycles) + "    idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }
  }

  template <class T>
  void PREA(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------PREA------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 0;

    assert(get_refreshing_bank_count<T>(node) == 0 && "PREA should not be called when there are refreshing banks");

    cur_power_stats.cmd_counters[T::m_cmds_counted("PRE")] += get_open_bank_count<T>(node);
    Rank::debug<T>(node, "Incrementing PRE counter.", clk);
    if (!is_rank_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is not idle. active_cycles: " + std::to_string(cur_power_stats.active_cycles) + "    idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }    
  }

  template <class T>
  void REFab(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------REFab------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    cur_power_stats.cmd_counters[T::m_cmds_counted("REF")]++;

    // We assume rank is idle when REF is called

    cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
    std::string msg = "Refresh starts. idle_cycles: " + std::to_string(cur_power_stats.idle_cycles);
    Rank::debug<T>(node, msg, clk);
    cur_power_stats.cur_power_state = PowerStats::PowerState::REFRESHING;
  }

  template <class T>
  void REFab_end(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------REFab_end------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];

    cur_power_stats.idle_start_cycle = clk;
    std::string msg = "Refresh ends. idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle);
    Rank::debug<T>(node, msg, clk);
    cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
  }

  template <class T>
  void VRR(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------VRR------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 0;

    if (is_rank_idle) {
      cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
      cur_power_stats.active_start_cycle = clk;
      std::string msg = "Rank is idle. idle_cycles: " + std::to_string(cur_power_stats.idle_cycles) + "    active_start_cycle: " + std::to_string(cur_power_stats.active_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::ACTIVE;
    }
  }
  
  template <class T>
  void VRR_end(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------VRR_end------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_going_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 1;

    if (is_rank_going_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is going idle. idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle) + "    active_cycles: " + std::to_string(cur_power_stats.active_cycles);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }
  }

  template <class T>
  void RFMsb(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------RFMsb------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 0;

    cur_power_stats.cmd_counters[T::m_cmds_counted("RFM")]++;
    if (is_rank_idle) {
      cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
      cur_power_stats.active_start_cycle = clk;
      std::string msg = "Rank is idle. idle_cycles: " + std::to_string(cur_power_stats.idle_cycles) + "    active_start_cycle: " + std::to_string(cur_power_stats.active_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::ACTIVE;
    }
  }

  template <class T>
  void RFMsb_end(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------RFMsb_end------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    size_t num_bankgroups = node->m_child_nodes.size();
    bool is_rank_going_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == num_bankgroups;

    if (is_rank_going_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is going idle. idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle) + "    active_cycles: " + std::to_string(cur_power_stats.active_cycles);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }
  }

  template <class T>
  void RRFMsb(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------RRFMsb------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    bool is_rank_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == 0;

    cur_power_stats.cmd_counters[T::m_cmds_counted("RRFM")]++;
    if (is_rank_idle) {
      cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
      cur_power_stats.active_start_cycle = clk;
      std::string msg = "Rank is idle. idle_cycles: " + std::to_string(cur_power_stats.idle_cycles) + "    active_start_cycle: " + std::to_string(cur_power_stats.active_start_cycle);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::ACTIVE;
    }
  }

  template <class T>
  void RRFMsb_end(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------RRFMsb_end------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];
    size_t num_bankgroups = node->m_child_nodes.size();
    bool is_rank_going_idle = get_open_bank_count<T>(node) == 0 && get_refreshing_bank_count<T>(node) == num_bankgroups;

    if (is_rank_going_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is going idle. idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle) + "    active_cycles: " + std::to_string(cur_power_stats.active_cycles);
      Rank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }
  }

  template <class T>
  void PREsb(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];

    int open_target_banks = 0;
    bool is_rank_going_idle = true;
    for (auto bankgroup_node : node->m_child_nodes) {
      for (auto bank_node : bankgroup_node->m_child_nodes) {
        if (bank_node->m_state == T::m_states["Opened"]) {
          if (bank_node->m_node_id == addr_vec[T::m_levels["bank"]]) {
            open_target_banks++;
          } else {
            is_rank_going_idle = false;
          }
        }
      }
    }

    cur_power_stats.cmd_counters[T::m_cmds_counted("PRE")] += open_target_banks;
    if (is_rank_going_idle) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
      cur_power_stats.idle_start_cycle = clk;
      std::string msg = "Rank is going idle. active_cycles: " + std::to_string(cur_power_stats.active_cycles) + "    idle_start_cycle: " + std::to_string(cur_power_stats.idle_start_cycle);
      Bank::debug<T>(node, msg, clk);
      cur_power_stats.cur_power_state = PowerStats::PowerState::IDLE;
    }
  }

  template <class T>
  void finalize_rank(typename T::Node* node, int cmd, const AddrVec_t& addr_vec, Clk_t clk) {
    Rank::debug<T>(node, "------finalize_rank------", clk);
    auto& cur_power_stats = node->m_spec->m_power_stats[Rank::get_flat_rank_id<T>(node)];

    if (cur_power_stats.cur_power_state == PowerStats::PowerState::IDLE) {
      cur_power_stats.idle_cycles += clk - cur_power_stats.idle_start_cycle;
    } else if (cur_power_stats.cur_power_state == PowerStats::PowerState::ACTIVE) {
      cur_power_stats.active_cycles += clk - cur_power_stats.active_start_cycle;
    } else if (cur_power_stats.cur_power_state == PowerStats::PowerState::REFRESHING) {
      // do nothing
    }
  }

}       // namespace Rank

}       // namespace Power
}       // namespace Lambdas
}       // namespace Ramulator

#endif  // RAMULATOR_DRAM_LAMBDAS_POWER_H