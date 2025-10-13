#include "memory_system/memory_system.h"

namespace Ramulator {

class DummyMemorySystem final : public IMemorySystem, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IMemorySystem, DummyMemorySystem, "DummyMemorySystem", "A dummy memory system with zero latency to test the frontend.");

  public:
    bool is_finished_ms() override {return true;};
    bool is_request_finished(Request req) override {return true;};
    int get_total_address_bits() override { return 0; };
    int get_shift_amt(int idx) override { return -1; };
    size_t get_max(int idx) override { return -1; };
    int get_num_channels() override {
      return -1;
    }

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