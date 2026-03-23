#include <fmt/format.h>

#include "ramulator/base/base.h"
#include "ramulator/memory_system/channel_mapper/i_channel_mapper.h"

namespace Ramulator {

class CacheLineInterleave final : public IChannelMapper, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IChannelMapper, CacheLineInterleave, "CacheLineInterleave");

  int m_interleave_bits;
  int m_num_channels = 1;
  int m_ch_shift = 0;
  int m_ch_width = 0;

 public:
  void init() override {
    RAMULATOR_PARSE_PARAM(m_interleave_bits, int, "interleave_bits").default_val(0);
  }

  void setup(int num_channels, int tx_offset) override {
    m_num_channels = num_channels;
    m_ch_shift = tx_offset + m_interleave_bits;
    // TODO: Support non-power-of-two channel counts via modulo/division-based mapping
    if (num_channels > 1 && (num_channels & (num_channels - 1)) != 0) {
      throw std::runtime_error(
          fmt::format("CacheLineInterleave requires a power-of-two channel count, got {}", num_channels));
    }
    m_ch_width = calc_log2(num_channels);
  }

  int get_channel(Addr_t addr) const override {
    if (m_num_channels <= 1) {
      return 0;
    }
    return (addr >> m_ch_shift) & ((1 << m_ch_width) - 1);
  }

  Addr_t get_intra_channel_addr(Addr_t addr) const override {
    if (m_num_channels <= 1) {
      return addr;
    }
    Addr_t low = addr & ((1LL << m_ch_shift) - 1);
    Addr_t high = addr >> (m_ch_shift + m_ch_width);
    return (high << m_ch_shift) | low;
  }
};

}  // namespace Ramulator
