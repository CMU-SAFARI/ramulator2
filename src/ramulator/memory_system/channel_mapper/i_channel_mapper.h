#ifndef RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H
#define RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H

#include "ramulator/base/base.h"

namespace Ramulator {

// Distributes requests across memory channels based on address bits.
class IChannelMapper {
  RAMULATOR_REGISTER_INTERFACE(IChannelMapper, "channel_mapper");

 public:
  virtual void setup(int num_channels, int tx_offset) = 0;
  virtual int get_channel(Addr_t addr) const = 0;
  virtual Addr_t get_intra_channel_addr(Addr_t addr) const = 0;
};

}  // namespace Ramulator

#endif  // RAMULATOR_MEMORY_SYSTEM_CHANNEL_MAPPER_I_CHANNEL_MAPPER_H
