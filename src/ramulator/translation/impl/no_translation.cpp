#include "ramulator/base/base.h"
#include "ramulator/base/param.h"
#include "ramulator/frontend/i_frontend.h"
#include "ramulator/translation/i_translation.h"

namespace Ramulator {

class NoTranslation : public ITranslation, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(ITranslation, NoTranslation, "NoTranslation");

 private:
  Addr_t m_max_paddr;  // Max physical address

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_max_paddr, Addr_t, "max_addr").required();
  };

  bool translate(Request& req) override {
    // No virtual-to-physical translation. Just wrap around max_paddr.
    req.addr = req.addr % m_max_paddr;
    return true;
  }
};

}  // namespace Ramulator