#ifndef RAMULATOR_MEMORY_SYSTEM_I_MEMORY_SYSTEM_H
#define RAMULATOR_MEMORY_SYSTEM_I_MEMORY_SYSTEM_H

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

class IMemorySystem : public TopLevel<IMemorySystem> {
  RAMULATOR_REGISTER_INTERFACE(IMemorySystem, "memory_system")

  friend class Factory;

 protected:
  IFrontEnd* m_frontend;

 public:
  // Calls setup() on m_impl first, then on all gathered child components.
  // Important: setup() implementations must NOT recursively call children's setup().
  virtual void connect_frontend(IFrontEnd* frontend) {
    m_frontend = frontend;
    m_impl->setup(frontend, this);
    for (auto component : m_components) {
      component->setup(frontend, this);
    }
  };

  void finalize() {
    m_impl->finalize();
    for (auto component : m_components) {
      component->finalize();
    }
  }

  void print_stats(std::ostream& os) { m_impl->print_stats(os); }
  ConfigNode collect_stats() const { return m_impl->collect_stats(); }

  virtual bool send(Request& req) = 0;
  virtual void tick() = 0;

  // Returns the clock ratio for the memory system (forwarded from controllers).
  virtual int get_clock_ratio() = 0;

  virtual float get_tCK() {
    return -1.0f;
  };

  // Returns the number of bytes per DRAM transaction (channel_width * internal_prefetch / 8).
  virtual int get_tx_bytes() = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_MEMORY_SYSTEM_I_MEMORY_SYSTEM_H
