#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"

namespace Ramulator {

class ExternalFrontEnd : public IFrontEnd, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IFrontEnd, ExternalFrontEnd, "External")

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").default_val(1);
  }

  void tick() override {}

  bool is_finished() override { return false; }

  bool receive_external_requests(int req_type_id, Addr_t addr, int source_id,
                                 std::function<void(Request&)> callback) override {
    Request req(addr, req_type_id, source_id, std::move(callback));
    return m_memory_system->send(req);
  }
};

}  // namespace Ramulator
