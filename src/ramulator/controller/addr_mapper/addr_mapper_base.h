#ifndef RAMULATOR_CONTROLLER_ADDR_MAPPER_ADDR_MAPPER_BASE_H
#define RAMULATOR_CONTROLLER_ADDR_MAPPER_ADDR_MAPPER_BASE_H

#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/controller_base.h"

namespace Ramulator {

class AddrMapperBase : public Implementation {
 protected:
  ControllerBase* m_ctrl;

  int m_num_mapped_levels = -1;  // level_count - 1 (skip channel)
  std::vector<int> m_addr_bits;  // bits per level (index 0 = first non-channel level)
  Addr_t m_tx_offset = -1;
  int m_row_idx = -1;  // index into m_addr_bits for Row
  int m_col_idx = -1;  // index into m_addr_bits for Column

 public:
  AddrMapperBase(const ConfigNode& config, Implementation* parent)
      : Implementation(config, "addr_mapper", "AddrMapperBase", parent), m_ctrl(dynamic_cast<ControllerBase*>(parent)) {
  }

  void init() override;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_ADDR_MAPPER_ADDR_MAPPER_BASE_H
