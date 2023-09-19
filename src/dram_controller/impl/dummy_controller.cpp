#include "dram_controller/controller.h"

namespace Ramulator {

class DummyController final : public IDRAMController, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IDRAMController, DummyController, "DummyController", "A dummy memory controller.");

  public:
    void init() override {
      return;
    };

    bool send(Request& req) override {
      if (req.callback) {
        req.callback(req);
      }
      return true; 
    };

    bool priority_send(Request& req) override {
      if (req.callback) {
        req.callback(req);
      }
      return true; 
    };

    void tick() override {
      return;
    }

};

}   // namespace Ramulator