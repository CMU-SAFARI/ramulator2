#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Row-Bank-Rank-Column-Channel (MSB to LSB in original scheme)
class RoBaRaCoCh : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, RoBaRaCoCh, AddrMapperBase, "RoBaRaCoCh")
  void init() override;
  void apply(Request& req) override;
};

void RoBaRaCoCh::init() {
  AddrMapperBase::init();
}

void RoBaRaCoCh::apply(Request& req) {
  req.addr_vec.resize(m_num_mapped_levels + 1, -1);
  Addr_t addr = req.intra_channel_addr >> m_tx_offset;
  // Column at LSB
  req.addr_vec[m_col_idx + 1] = slice_lower_bits(addr, m_addr_bits[m_col_idx]);
  // Rank through Row (levels 0 to row_idx in m_addr_bits order)
  for (int i = 0; i <= m_row_idx; i++) {
    req.addr_vec[i + 1] = slice_lower_bits(addr, m_addr_bits[i]);
  }
}

}  // namespace Ramulator
