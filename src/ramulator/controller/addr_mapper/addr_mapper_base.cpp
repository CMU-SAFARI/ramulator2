#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

#include <stdexcept>

#include <fmt/format.h>

namespace Ramulator {

namespace {

// Bit-slicing in the addr_mapper assumes every level dimension is a
// power of two: it allocates calc_log2(size) bits and decodes via
// `addr & ((1 << bits) - 1)`. When `size` is not a power of two the
// allocated field can't represent every index — e.g. size = 3 gets
// only 1 bit, silently aliasing index 2 onto index 0. Catch this at
// init time with a clear message naming the offending level, instead
// of letting the simulation produce wrong results.
bool is_power_of_two(int n) { return n > 0 && (n & (n - 1)) == 0; }

}  // namespace

void AddrMapperBase::init() {
  const auto& dram_spec = *m_ctrl->m_device.m_spec;
  const auto& level_sizes = dram_spec.organization.level_sizes;
  m_num_mapped_levels = dram_spec.level_count - 1;  // skip channel
  m_addr_bits.resize(m_num_mapped_levels);
  for (int i = 0; i < m_num_mapped_levels; i++) {
    int size = level_sizes[i + 1];
    if (!is_power_of_two(size)) {
      const std::string& level_name = dram_spec.level_names[i + 1];
      throw std::runtime_error(fmt::format(
          "AddrMapperBase: level '{}' has size {} which is not a power of "
          "two — bit-sliced address mapping cannot encode it without "
          "aliasing. Either change the org preset to a power-of-two "
          "dimension or extend the addr_mapper to support modulo "
          "addressing.",
          level_name, size));
    }
    m_addr_bits[i] = calc_log2(size);
  }

  // Column adjusted for prefetch
  m_addr_bits[m_num_mapped_levels - 1] -= calc_log2(dram_spec.internal_prefetch_size);

  // Transaction offset
  m_tx_offset = calc_log2(dram_spec.get_tx_bytes());

  // Level indices (offset by 1 since channel is removed)
  m_row_idx = dram_spec.get_level_id("Row") - 1;
  m_col_idx = dram_spec.level_count - 2;  // Column is always last mapped level
}

}  // namespace Ramulator
