#include <stdexcept>

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
    // A negative interleave_bits drives m_ch_shift below tx_offset on setup()
    // and turns every shift in apply() into undefined behavior (negative
    // shift amounts are UB in C++). Reject it loudly at construction.
    if (m_interleave_bits < 0) {
      throw std::runtime_error(fmt::format(
          "CacheLineInterleave: interleave_bits must be >= 0 (got {})",
          m_interleave_bits));
    }
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
    // apply() does `(1LL << m_ch_shift)` and `req.addr >> (m_ch_shift + m_ch_width)`.
    // Addr_t is int64_t, so shifts >= 64 are undefined behavior. The combined
    // m_ch_shift + m_ch_width is the deepest shift; cap it at one bit under
    // the 64-bit type width. (Reaching this in practice would mean a single
    // channel addresses >= 2^63 bytes, but the check is cheap and the failure
    // mode is otherwise silently-wrong addresses.)
    if (m_ch_shift + m_ch_width >= 64) {
      throw std::runtime_error(fmt::format(
          "CacheLineInterleave: tx_offset({}) + interleave_bits({}) + "
          "log2(num_channels)({}) = {} exceeds the 63-bit Addr_t shift range",
          tx_offset, m_interleave_bits, m_ch_width, m_ch_shift + m_ch_width));
    }
  }

  void apply(Request& req) const override {
    if (req.addr_vec.empty()) {
      req.addr_vec.resize(1, -1);
    }
    if (m_num_channels <= 1) {
      req.addr_vec[0] = 0;
      req.intra_channel_addr = req.addr;
      return;
    }
    // Widen `1` to a 64-bit literal: m_ch_width can in principle reach 31
    // (>= 2^31 channels), and `(1 << 31)` on a 32-bit int is signed shift
    // overflow / UB. Match the sibling `(1LL << m_ch_shift)` literal below.
    req.addr_vec[0] = (req.addr >> m_ch_shift) & ((1LL << m_ch_width) - 1);
    Addr_t low = req.addr & ((1LL << m_ch_shift) - 1);
    Addr_t high = req.addr >> (m_ch_shift + m_ch_width);
    req.intra_channel_addr = (high << m_ch_shift) | low;
  }
};

}  // namespace Ramulator
