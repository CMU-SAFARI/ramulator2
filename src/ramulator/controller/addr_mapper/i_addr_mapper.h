#ifndef RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H
#define RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H

#include "ramulator/base/base.h"
#include "ramulator/base/request.h"

namespace Ramulator {

// Maps a request's intra-channel address to DRAM hierarchy coordinates.
// Flat-address mappers read req.intra_channel_addr and write req.addr_vec[1..N-1].
// PassThrough mapper does nothing (addr_vec already populated by frontend).
class IAddrMapper {
  RAMULATOR_REGISTER_INTERFACE(IAddrMapper, "addr_mapper")
 public:
  virtual void apply(Request& req) = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_ADDR_MAPPER_I_ADDR_MAPPER_H
