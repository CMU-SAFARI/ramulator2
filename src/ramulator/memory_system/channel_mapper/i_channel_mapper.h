#ifndef RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H
#define RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H

#include "ramulator/base/base.h"
#include "ramulator/base/request.h"

namespace Ramulator {

// Distributes requests across memory channels based on address bits.
// Populates req.addr_vec[0] (channel ID) and req.intra_channel_addr.
class IChannelMapper {
  RAMULATOR_REGISTER_INTERFACE(IChannelMapper, "channel_mapper");

 public:
  virtual void setup(int num_channels, int tx_offset) = 0;

  // Map the request to a channel: set req.addr_vec[0] and req.intra_channel_addr.
  virtual void apply(Request& req) const = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H
