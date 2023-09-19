#include "rit.h"

namespace Ramulator {

void LinearMapperBase_with_rit::setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
  m_dram = memory_system->get_ifce<IDRAM>();

  // Populate m_addr_bits vector with the number of address bits for each level in the hierachy
  const auto& count = m_dram->m_organization.count;
  m_num_levels = count.size();
  m_addr_bits.resize(m_num_levels);
  for (size_t level = 0; level < m_addr_bits.size(); level++) {
    m_addr_bits[level] = calc_log2(count[level]);
  }

  // Last (Column) address have the granularity of the prefetch size
  m_addr_bits[m_num_levels - 1] -= calc_log2(m_dram->m_internal_prefetch_size);

  int tx_bytes = m_dram->m_internal_prefetch_size * m_dram->m_channel_width / 8;
  m_tx_offset = calc_log2(tx_bytes);

  // Determine where are the row and col bits for ChRaBaRoCo and RoBaRaCoCh
  try {
    m_row_bits_idx = m_dram->m_levels("row");
  } catch (const std::out_of_range& r) {
    throw std::runtime_error(fmt::format("Organization \"row\" not found in the spec, cannot use linear mapping!"));
  }

  // Assume column is always the last level
  m_col_bits_idx = m_num_levels - 1;
}

// initialize RIT
void LinearMapperBase_with_rit::init_rit(int num_banks, int num_rit_entries){
  m_num_rit_entries = num_rit_entries;
  m_rank_level = m_dram->m_levels("rank");
  m_bank_level = m_dram->m_levels("bank");
  m_row_level = m_dram->m_levels("row");

  // setup RIT
  for (int i = 0; i < num_banks; i++) {
    std::unordered_map<int, RIT_entry> rit_bank;
    m_row_indirection_table.push_back(rit_bank);
  }
}

// check if the entry is in the RIT
int LinearMapperBase_with_rit::check_rit(int flat_bank_id, int src_row){
  if (m_row_indirection_table[flat_bank_id].find(src_row) != m_row_indirection_table[flat_bank_id].end()) {
    return m_row_indirection_table[flat_bank_id][src_row].dst_row;
  }
  return -1;
}

// check if the RIT is full
bool LinearMapperBase_with_rit::is_rit_full(int flat_bank_id){
  return m_row_indirection_table[flat_bank_id].size() >= m_num_rit_entries;
}

// check if the entry is locked
bool LinearMapperBase_with_rit::is_rit_locked(int flat_bank_id, int src_row){
  return m_row_indirection_table[flat_bank_id][src_row].lock;
}

// performs the indirection if the row is in the RIT
void LinearMapperBase_with_rit::apply_indirection(Request& req){
  if (m_num_rit_entries == -1){
    // RIT is not initiliazed, indirection won't be performed.
    return;
  }

  int flat_bank_id = req.addr_vec[m_bank_level];
  int accumulated_dimension = 1;
  for (int i = m_bank_level - 1; i >= m_rank_level; i--) {
    accumulated_dimension *= m_dram->m_organization.count[i + 1];
    flat_bank_id += req.addr_vec[i] * accumulated_dimension;
  }
  int src_row = req.addr_vec[m_row_level];
  int dst_row = -1;
  // check if the row is in the RIT
  dst_row = check_rit(flat_bank_id, src_row);

  // if dst_row is found, update the request row address, otherwise, do nothing
  if (dst_row != -1) {
    req.addr_vec[m_row_level] = dst_row;
  }
}

// unlocks all the entries in the RIT at the end of each Epoch
void LinearMapperBase_with_rit::rit_unlock() {
  for (auto& bank : m_row_indirection_table) {
    for (auto& entry : bank) {
      entry.second.lock = false;
    }
  }
}

// inserts the entry and its pair into the RIT
void LinearMapperBase_with_rit::rit_insert_entry(int flat_bank_id, int src_row, int dst_row) {
  // insert the entry into the RIT
  RIT_entry entry0;
  entry0.dst_row = dst_row;
  entry0.lock = true;
  m_row_indirection_table[flat_bank_id][src_row] = entry0;
  // insert the pair of entry into the RIT
  RIT_entry entry1;
  entry1.dst_row = src_row;
  entry1.lock = true;
  m_row_indirection_table[flat_bank_id][dst_row] = entry1;

  if(m_row_indirection_table[flat_bank_id].size() > m_num_rit_entries){
    std::cerr << "RIT is full!!!!!!!!!! Check before insertion." << std::endl;
    exit(1);
  }
}

// removes the entry and its pair from the RIT
void LinearMapperBase_with_rit::rit_remove_entry(int flat_bank_id, int src_row, int dst_row) {
  // remove the entry from the RIT
  m_row_indirection_table[flat_bank_id].erase(src_row);
  // remove the pair of entry from the RIT
  m_row_indirection_table[flat_bank_id].erase(dst_row);
}

// gets a pair of entries from the RIT to unswap, the pair cannot be in the exclusion_list
std::pair<int, int> LinearMapperBase_with_rit::get_unswap_pair(int flat_bank_id, const std::unordered_map<int, int>& exclusion_list){
  std::pair<int, int> unswap_pair;
  for (auto& entry : m_row_indirection_table[flat_bank_id]) {
    if (!entry.second.lock && exclusion_list.find(entry.first) == exclusion_list.end() && exclusion_list.find(entry.second.dst_row) == exclusion_list.end()) {
      unswap_pair.first = entry.first;
      unswap_pair.second = entry.second.dst_row;
      return unswap_pair;
    }
  }
  std::cerr << "No unlocked entry found in the RIT! Should not happen!" << std::endl;
  exit(1);
}

// dumps RIT for debug
void LinearMapperBase_with_rit::dump_rit(int flat_bank_id) {
  std::cout << "======================" << std::endl
            << "RIT[" << flat_bank_id << "].size(): " << m_row_indirection_table[flat_bank_id].size() << std::endl;

  for (auto entry: m_row_indirection_table[flat_bank_id]){
    std::cout << entry.first << " -> " << entry.second.dst_row << "\t" << (entry.second.lock ? "locked": "unlocked") << std::endl;
  }
  std::cout << "======================" << std::endl;
}

class ChRaBaRoCo_with_rit final : public LinearMapperBase_with_rit, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, ChRaBaRoCo_with_rit, "ChRaBaRoCo_with_rit", "Applies a trival mapping to the address.");

  public:
    void init() override { };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      LinearMapperBase_with_rit::setup(frontend, memory_system);
    }

    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);
      Addr_t addr = req.addr >> m_tx_offset;
      for (int i = m_addr_bits.size() - 1; i >= 0; i--) {
        req.addr_vec[i] = slice_lower_bits(addr, m_addr_bits[i]);
      }
      // perform indirection
      LinearMapperBase_with_rit::apply_indirection(req);
    }
};


class RoBaRaCoCh_with_rit final : public LinearMapperBase_with_rit, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, RoBaRaCoCh_with_rit, "RoBaRaCoCh_with_rit", "Applies a RoBaRaCoCh mapping to the address.");

  public:
    void init() override { };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      LinearMapperBase_with_rit::setup(frontend, memory_system);
    }

    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);
      Addr_t addr = req.addr >> m_tx_offset;
      req.addr_vec[0] = slice_lower_bits(addr, m_addr_bits[0]);
      req.addr_vec[m_addr_bits.size() - 1] = slice_lower_bits(addr, m_addr_bits[m_addr_bits.size() - 1]);
      for (int i = 1; i <= m_row_bits_idx; i++) {
        req.addr_vec[i] = slice_lower_bits(addr, m_addr_bits[i]);
      }
      // perform indirection
      LinearMapperBase_with_rit::apply_indirection(req);
    }
};


class MOP4CLXOR_with_rit final : public LinearMapperBase_with_rit, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, MOP4CLXOR_with_rit, "MOP4CLXOR_with_rit", "Applies a MOP4CLXOR mapping to the address.");

  public:
    void init() override { };

    void setup(IFrontEnd* frontend, IMemorySystem* memory_system) override {
      LinearMapperBase_with_rit::setup(frontend, memory_system);
    }

    void apply(Request& req) override {
      req.addr_vec.resize(m_num_levels, -1);
      Addr_t addr = req.addr >> m_tx_offset;
      req.addr_vec[m_col_bits_idx] = slice_lower_bits(addr, 2);
      for (int lvl = 0 ; lvl < m_row_bits_idx ; lvl++)
          req.addr_vec[lvl] = slice_lower_bits(addr, m_addr_bits[lvl]);
      req.addr_vec[m_col_bits_idx] += slice_lower_bits(addr, m_addr_bits[m_col_bits_idx]-2) << 2;
      req.addr_vec[m_row_bits_idx] = (int) addr;

      int row_xor_index = 0; 
      for (int lvl = 0 ; lvl < m_col_bits_idx ; lvl++){
        if (m_addr_bits[lvl] > 0){
          int mask = (req.addr_vec[m_col_bits_idx] >> row_xor_index) & ((1<<m_addr_bits[lvl])-1);
          req.addr_vec[lvl] = req.addr_vec[lvl] xor mask;
          row_xor_index += m_addr_bits[lvl];
        }
      }
      // perform indirection
      LinearMapperBase_with_rit::apply_indirection(req);
    }
};

}   // namespace Ramulator