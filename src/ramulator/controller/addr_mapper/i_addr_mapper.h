#ifndef RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H
#define RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H

#include "ramulator/base/base.h"

namespace Ramulator {

// Maps a flat physical address to DRAM hierarchy coordinates (rank, bank, row, column).
class IAddrMapper {
  RAMULATOR_REGISTER_INTERFACE(IAddrMapper, "addr_mapper")
 public:
  // Maps a channel-stripped address into addr_vec[1..COUNT-1].
  virtual void apply(Addr_t stripped_addr, AddrVec_t& addr_vec) = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H
