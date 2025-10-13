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
    
    size_t get_read_queue_length() override {
      return 0; 
    };
    size_t get_write_queue_length() override {
      return 0; 
    };
    size_t get_active_buffer_length() override {
      return 0; 
    };

    bool is_req_in_read_queue(Request req) override {return true;};
    bool is_req_in_pending_queue(Request req) override {return true;};

};

}   // namespace Ramulator