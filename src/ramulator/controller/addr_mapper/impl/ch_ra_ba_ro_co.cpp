#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Channel-Rank-BankGroup-Bank-Row-Column (MSB to LSB)
class ChRaBaRoCo : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, ChRaBaRoCo, AddrMapperBase, "ChRaBaRoCo")
  void init() override;
  void apply(Request& req) override;
};

void ChRaBaRoCo::init() {
  AddrMapperBase::init();
}

void ChRaBaRoCo::apply(Request& req) {
  req.addr_vec.resize(m_num_mapped_levels + 1, -1);
  Addr_t addr = req.intra_channel_addr >> m_tx_offset;
  // Extract from LSB to MSB: Column, Row, ..., Rank
  for (int i = m_num_mapped_levels - 1; i >= 0; i--) {
    req.addr_vec[i + 1] = slice_lower_bits(addr, m_addr_bits[i]);
  }
}

}  // namespace Ramulator
