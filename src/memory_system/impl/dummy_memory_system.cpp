#include "memory_system/memory_system.h"

namespace Ramulator {

class DummyMemorySystem final : public IMemorySystem, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, DummyMemorySystem, "DummyMemorySystem", "A dummy memory system with zero latency to test the frontend.");

  public:
    void init() override {
      m_clock_ratio = param<uint>("clock_ratio").default_val(1);
    };

    bool send(Request req) override { 
      if (req.callback) {
        req.callback(req);
      }
      return true; 
    };

    void tick() override {};
};
  
}   // namespace Ramulator