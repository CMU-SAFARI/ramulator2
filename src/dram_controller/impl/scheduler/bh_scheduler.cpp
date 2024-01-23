#include <vector>

#include "base/base.h"
#include "dram_controller/bh_controller.h"
#include "dram_controller/bh_scheduler.h"

namespace Ramulator {

class BHScheduler : public IBHScheduler, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IBHScheduler, BHScheduler, "BHScheduler", "BHammer Scheduler.")

  private:
    IDRAM* m_dram;

    int m_clk = -1;

    bool m_is_debug; 

  public:
    void init() override {
    }

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      m_dram = cast_parent<IDRAMController>()->m_dram;
    }

    ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) override {
      bool ready1 = m_dram->check_ready(req1->command, req1->addr_vec);
      bool ready2 = m_dram->check_ready(req2->command, req2->addr_vec);

      if (ready1 ^ ready2) {
        if (ready1) {
          return req1;
        } else {
          return req2;
        }
      }

      // Fallback to FCFS
      if (req1->arrive <= req2->arrive) {
        return req1;
      } else {
        return req2;
      } 
    }

    ReqBuffer::iterator get_best_request(ReqBuffer& buffer) override {
      if (buffer.size() == 0) {
        return buffer.end();
      }

      for (auto& req : buffer) {
        req.command = m_dram->get_preq_command(req.final_command, req.addr_vec);
      }

      auto candidate = buffer.begin();
      for (auto next = std::next(buffer.begin(), 1); next != buffer.end(); next++) {
        candidate = compare(candidate, next);
      }
      return candidate;
    }

    virtual void tick() override {
      m_clk++;
    }
};

}       // namespace Ramulator
