#ifndef RAMULATOR_PLUGIN_BLOCKHAMMER_THROTTLER_
#define RAMULATOR_PLUGIN_BLOCKHAMMER_THROTTLER_

#include "frontend/impl/processor/bhO3/bhllc.h"

namespace Ramulator {
class AttackThrottler {
public:
  AttackThrottler(BHO3LLC* llc, int n_rh, int n_bl, int t_cbf, int t_refw, int n_ctrs) {
    m_n_rh = n_rh; 
    m_n_bl = n_bl; 
    m_t_cbf = t_cbf; 
    m_t_refw = t_refw; 
    m_n_ctrs = n_ctrs;
    m_active_idx = 0;
    m_llc = llc;
    for (int i = 0; i < n_ctrs; i++) {
      m_act_counters.push_back(new std::unordered_map<int, int>);
    }
  }

  void update() {
    m_clk++;
    if (m_clk >= m_t_cbf) {
      m_clk = 0;
      m_act_counters[m_active_idx]->clear();
      m_active_idx = (m_active_idx + 1) % m_n_ctrs;
    }
  }

  void insert(int thread_id, int bank_id) {
    auto key = hash(thread_id, bank_id);
    for (int i = 0; i < m_n_ctrs; i++) {
      auto& counter_map = *m_act_counters[i];
      if (counter_map.find(key) == counter_map.end()) {
        counter_map[key] = 0;
      }
      counter_map[key]++;
    }
  }

  float get_rhli(int thread_id, int bank_id) {
    if (thread_id < 0) { 
      return 0.0f;
    }
    auto& counter_map = *m_act_counters[m_active_idx];
    auto key = hash(thread_id, bank_id);
    if (counter_map.find(key) == counter_map.end()) {
      return 0.0f;
    }
    float rhli = (float) counter_map[key] / (m_n_rh * (float) m_t_cbf / m_t_refw - m_n_bl);
    return rhli;
  }

  int hash(int thread_id, int bank_id) {
    return thread_id * 100000 + bank_id;
  }

private:
  int m_clk = -1;
  int m_n_rh = -1;
  int m_n_bl = -1;
  int m_t_cbf = -1;
  int m_t_refw = -1;
  int m_n_ctrs = -1;
  int m_active_idx = -1;
  std::vector<std::unordered_map<int, int>*> m_act_counters;
  std::vector<int> m_blacklisted_threads;
  BHO3LLC* m_llc = nullptr;
};      // class AttackThrottler
}; // namespace Ramulator

#endif // RAMULATOR_PLUGIN_BLOCKHAMMER_THROTTLER_