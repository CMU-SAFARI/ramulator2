#ifndef     RAMULATOR_MEMORYSYSTEM_MEMORY_H
#define     RAMULATOR_MEMORYSYSTEM_MEMORY_H

#include <map>
#include <vector>
#include <string>
#include <functional>

#include "base/base.h"
#include "frontend/frontend.h"

namespace Ramulator {

class IMemorySystem : public TopLevel<IMemorySystem> {
  RAMULATOR_REGISTER_INTERFACE(IMemorySystem, "MemorySystem", "Memory system interface (e.g., communicates between processor and memory controller).")

  friend class Factory;

  protected:
    IFrontEnd* m_frontend;
    uint m_clock_ratio = 1;

  public:
    virtual void connect_frontend(IFrontEnd* frontend) { 
      m_frontend = frontend; 
      m_impl->setup(frontend, this);
      for (auto component : m_components) {
        component->setup(frontend, this);
      }
    };

    virtual void finalize() { 
      for (auto component : m_components) {
        component->finalize();
      }

      YAML::Emitter emitter;
      emitter << YAML::BeginMap;
      m_impl->print_stats(emitter);
      emitter << YAML::EndMap;
      std::cout << emitter.c_str() << std::endl;
    };

    /**
     * @brief         Tries to send the request to the memory system
     * 
     * @param    req      The request
     * @return   true     Request is accepted by the memory system.
     * @return   false    Request is rejected by the memory system, maybe the memory controller is full?
     */
    virtual bool send(Request req) = 0;

    /**
     * @brief         Ticks the memory system
     * 
     */
    virtual void tick() = 0;

    /**
     * @brief    Returns 
     * 
     * @return   int 
     */
    int get_clock_ratio() { return m_clock_ratio; };

    // /**
    //  * @brief    Get the integer id of the request type from the memory spec
    //  * 
    //  */
    // virtual const SpecDef& get_supported_requests() = 0;

    virtual float get_tCK() { return -1.0f; };
};

}        // namespace Ramulator


#endif   // RAMULATOR_MEMORYSYSTEM_MEMORY_H