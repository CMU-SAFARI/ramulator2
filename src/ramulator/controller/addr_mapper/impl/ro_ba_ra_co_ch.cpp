#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Row-Bank-Rank-Column-Channel (MSB to LSB in original scheme)
class RoBaRaCoCh : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, RoBaRaCoCh, AddrMapperBase, "RoBaRaCoCh")
  void init() override;
  void apply(Addr_t addr, AddrVec_t& addr_vec) override;
};

void RoBaRaCoCh::init() {
  AddrMapperBase::init();
}

void RoBaRaCoCh::apply(Addr_t addr, AddrVec_t& addr_vec) {
  addr >>= m_tx_offset;
  // Column at LSB
  addr_vec[m_col_idx + 1] = slice_lower_bits(addr, m_addr_bits[m_col_idx]);
  // Rank through Row (levels 0 to row_idx in m_addr_bits order)
  for (int i = 0; i <= m_row_idx; i++) {
    addr_vec[i + 1] = slice_lower_bits(addr, m_addr_bits[i]);
  }
}

}  // namespace Ramulator
