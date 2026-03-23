#include "ramulator/controller/addr_mapper/i_addr_mapper.h"
#include "ramulator/controller/addr_mapper/addr_mapper_base.h"

namespace Ramulator {

// Multi-Offset Physical with 4-CL XOR
class MOP4CLXOR : public IAddrMapper, public AddrMapperBase {
  RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(IAddrMapper, MOP4CLXOR, AddrMapperBase, "MOP4CLXOR")
  void init() override;
  void apply(Addr_t addr, AddrVec_t& addr_vec) override;
};

void MOP4CLXOR::init() {
  AddrMapperBase::init();
}

void MOP4CLXOR::apply(Addr_t addr, AddrVec_t& addr_vec) {
  addr >>= m_tx_offset;
  // Low 2 column bits
  addr_vec[m_col_idx + 1] = slice_lower_bits(addr, 2);
  // Levels 0 through row_idx-1 (rank, bankgroup, bank, etc.)
  for (int i = 0; i < m_row_idx; i++) {
    addr_vec[i + 1] = slice_lower_bits(addr, m_addr_bits[i]);
  }
  // Remaining column bits
  addr_vec[m_col_idx + 1] += slice_lower_bits(addr, m_addr_bits[m_col_idx] - 2) << 2;
  // Row bits (whatever remains)
  addr_vec[m_row_idx + 1] = static_cast<int>(addr);

  // XOR row bits with column bits
  int row_xor_index = 0;
  for (int i = 0; i < m_col_idx; i++) {
    if (m_addr_bits[i] > 0) {
      int mask = (addr_vec[m_col_idx + 1] >> row_xor_index) & ((1 << m_addr_bits[i]) - 1);
      addr_vec[i + 1] = addr_vec[i + 1] ^ mask;
      row_xor_index += m_addr_bits[i];
    }
  }
}

}  // namespace Ramulator
