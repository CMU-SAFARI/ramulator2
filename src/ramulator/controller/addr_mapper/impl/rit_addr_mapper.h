#ifndef RAMULATOR_CONTROLLER_ADDR_MAPPER_RIT_ADDR_MAPPER_H
#define RAMULATOR_CONTROLLER_ADDR_MAPPER_RIT_ADDR_MAPPER_H

#include <unordered_map>
#include <utility>
#include <vector>

#include "ramulator/base/base.h"
#include "ramulator/controller/addr_mapper/i_addr_mapper.h"

namespace Ramulator {

class ControllerBase;

// This header is included by plugins RRS, AQUA that need to modify the RIT
class RITAddrMapper : public IAddrMapper, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(IAddrMapper, RITAddrMapper, "RITAddrMapper")

 public:
  void init() override;
  void apply(Request& req) override;

  // ── RIT API ──────────────────────────────────
  void init_rit(int num_banks, int max_entries_per_bank);
  int check_rit(int bank, int row) const;
  bool is_rit_locked(int bank, int row) const;
  bool is_rit_full(int bank) const;
  void insert_entry(int bank, int src_row, int dst_row);
  void remove_entry(int bank, int src_row, int dst_row);
  std::pair<int, int> get_unswap_pair(int bank,
                                      const std::unordered_map<int, int>& hrt) const;
  void rit_unlock();
  void dump_rit(int bank) const;

 private:
  // Construct the wrapped base mapper.
  IAddrMapper* create_base_mapper();

  // Wrapped Base Mapper: handles flat-address decomposition. RIT holds the
  // pointer, but the inner mapper is constructed with the controller as
  // its formal parent.
  IAddrMapper* m_base_mapper = nullptr;

  ControllerBase* m_ctrl = nullptr;
  int m_row_idx = -1;  // row index inside addr_vec
  int m_reserved_rows_per_bank = 0;
  int m_num_rows_per_bank = -1;  // bank capacity, used for bounds check on shifted row

  // RIT state — per-bank logical→physical row indirection.
  struct RITEntry {
    int swapped_row = -1;
    bool locked = false;
  };
  std::vector<std::unordered_map<int, RITEntry>> m_rit;
  int m_max_entries_per_bank = 0;
  bool m_initialized = false;
};

}  // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_ADDR_MAPPER_RIT_ADDR_MAPPER_H
