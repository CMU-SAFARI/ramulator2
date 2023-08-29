#include <iostream>
#include <vector>
#include <random>

#include "ramulator/base.h"
#include "translation/translation.h"
#include "ramulator/frontend.h"


namespace Ramulator {

class NoTranslation: public ITranslation, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(ITranslation, NoTranslation, "NoTranslation", "Use the virtual address as physical address.");
  private:
    Addr_t m_max_paddr;         // Max physical address

  public:
    void init() override { 
      m_max_paddr = param<Addr_t>("max_addr").desc("Max physical address of the memory system.").required();
    };

    bool translate(Request& req) override {
      // We dont do any translation. Just wrap the vaddr around max_paddr.
      // Addr_t new_addr = (req.addr % m_max_paddr);
      Addr_t new_addr = (req.addr);
      req.addr = new_addr;
      return true;
    }
};

}   // namespace Ramulator