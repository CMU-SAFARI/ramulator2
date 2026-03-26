#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

void AddrMapperBase::init() {
  const auto& dram_spec = *m_ctrl->m_device.m_spec;
  const auto& level_sizes = dram_spec.organization.level_sizes;
  m_num_mapped_levels = dram_spec.level_count - 1;  // skip channel
  m_addr_bits.resize(m_num_mapped_levels);
  for (int i = 0; i < m_num_mapped_levels; i++) {
    m_addr_bits[i] = calc_log2(level_sizes[i + 1]);
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
