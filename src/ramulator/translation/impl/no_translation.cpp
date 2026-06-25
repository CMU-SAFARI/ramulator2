#include <stdexcept>

#include <fmt/format.h>

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
    // translate() does `req.addr % m_max_paddr` on every request.
    // max_addr <= 0 turns that into integer modulo-by-zero or
    // undefined-result modulo by a negative divisor, which manifests
    // as a SIGFPE core dump partway through the simulation rather
    // than a clear configuration error at construction.
    if (m_max_paddr <= 0) {
      throw std::runtime_error(fmt::format(
          "NoTranslation: max_addr must be > 0 (got {})", m_max_paddr));
    }
  };

  bool translate(Request& req) override {
    // No virtual-to-physical translation. Just wrap around max_paddr.
    req.addr = req.addr % m_max_paddr;
    return true;
  }
};

}  // namespace Ramulator