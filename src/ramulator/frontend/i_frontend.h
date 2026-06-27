#ifndef RAMULATOR_FRONTEND_I_FRONTEND_H
#define RAMULATOR_FRONTEND_I_FRONTEND_H

#include <functional>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/memory_system/i_memory_system.h"

namespace Ramulator {

class IFrontEnd : public TopLevel<IFrontEnd> {
  RAMULATOR_REGISTER_INTERFACE(IFrontEnd, "frontend");

  friend class Factory;

 protected:
  Clk_t m_clk = 0;
  IMemorySystem* m_memory_system;
  unsigned int m_clock_ratio = 1;

 public:
  // Calls setup() on m_impl first, then on all gathered child components.
  // Important: setup() implementations must NOT recursively call children's setup().
  virtual void connect_memory_system(IMemorySystem* memory_system) {
    m_memory_system = memory_system;
    m_impl->setup(this, memory_system);
    for (auto component : m_components) {
      component->setup(this, memory_system);
    }
  };

  virtual void tick() = 0;
  virtual bool is_finished() = 0;

  void finalize() {
    m_impl->finalize();
    for (auto component : m_components) {
      component->finalize();
    }
  }

  void reset_stats_recursive() {
    m_impl->reset_stats();
    for (auto component : m_components) {
      component->reset_stats();
    }
  }
  void print_stats(std::ostream& os) { m_impl->print_stats(os); }
  ConfigNode collect_stats() const { return m_impl->collect_stats(); }

  virtual int get_num_cores() {
    return 1;
  };

  int get_clock_ratio() {
    return m_clock_ratio;
  };

  /**
   * @brief    Receives memory requests from external sources (e.g., coming from a full system simulator like GEM5)
   *
   * @details
   * This functions should take memory requests from external sources (e.g., coming from GEM5), generate Ramulator 2
   * Requests, (tries to) send to the memory system, and return if this is successful
   *
   */
  virtual bool receive_external_requests(int req_type_id, Addr_t addr, int source_id,
                                         std::function<void(Request&)> callback,
                                         int size_bytes) {
    return false;
  }
};

}  // namespace Ramulator

#endif  // RAMULATOR_FRONTEND_I_FRONTEND_H
