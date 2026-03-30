#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Pass-through: assumes addr_vec is already populated by the frontend.
// Used with addr_vec-native frontends (LatencyThroughputTrace, ReadWriteTrace).
class PassThroughAddrMapper : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, PassThroughAddrMapper, AddrMapperBase, "PassThroughAddrMapper")
  void init() override;
  void apply(Request& req) override;
};

void PassThroughAddrMapper::init() {
  AddrMapperBase::init();
}

void PassThroughAddrMapper::apply(Request&) {
  // addr_vec already populated by frontend — nothing to do
}

}  // namespace Ramulator
