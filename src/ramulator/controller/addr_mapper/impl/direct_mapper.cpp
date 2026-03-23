#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Pass-through: assumes addr_vec is already populated by the frontend
class DirectMapper : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, DirectMapper, AddrMapperBase, "DirectMapper")
  void init() override;
  void apply(Addr_t, AddrVec_t&) override;
};

void DirectMapper::init() {
  AddrMapperBase::init();
}

void DirectMapper::apply(Addr_t, AddrVec_t&) {
  // addr_vec already populated by frontend
}

}  // namespace Ramulator
