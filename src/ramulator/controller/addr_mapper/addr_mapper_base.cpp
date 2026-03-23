#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

void AddrMapperBase::init() {
  const auto& info = *m_ctrl->m_device.m_spec;
  const auto& count = info.organization.level_sizes;
  m_num_mapped_levels = info.level_count - 1;  // skip channel
  m_addr_bits.resize(m_num_mapped_levels);
  for (int i = 0; i < m_num_mapped_levels; i++) {
    m_addr_bits[i] = calc_log2(count[i + 1]);  // count[1..COUNT-1]
  }

  // Column adjusted for prefetch
  m_addr_bits[m_num_mapped_levels - 1] -= calc_log2(info.internal_prefetch_size);

  // Transaction offset
  int tx_bytes = info.internal_prefetch_size * info.channel_width / 8;
  m_tx_offset = calc_log2(tx_bytes);

  // Level indices (offset by 1 since channel is removed)
  m_row_idx = info.get_level_id("Row") - 1;
  m_col_idx = info.level_count - 2;  // Column is always last mapped level
}

}  // namespace Ramulator
