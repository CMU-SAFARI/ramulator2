// Row Indirection Table — decorator addr mapper. apply() runs the
// wrapped base mapper, then rewrites the row via the RIT swap table
// (set up by RRS/AQUA), then shifts the row above reserved_rows_per_bank
// (used by Hydra/AQUA for controller-internal regions). The base
// mapper is constructed with the controller as its parent so existing
// AddrMapperBase impls reach back to ControllerBase normally.
#include "ramulator/controller/addr_mapper/impl/rit_addr_mapper.h"

#include <iostream>
#include <stdexcept>

#include "ramulator/base/factory.h"
#include "ramulator/base/param.h"
#include "ramulator/controller/controller_base.h"
#include "ramulator/dram/dram_spec.h"

namespace Ramulator {

void RITAddrMapper::init() {
  RAMULATOR_PARSE_PARAM(m_reserved_rows_per_bank, int, "reserved_rows_per_bank").default_val(0);
  if (m_reserved_rows_per_bank < 0) {
    throw std::runtime_error("RITAddrMapper: reserved_rows_per_bank must be non-negative");
  }

  m_ctrl = cast_parent<ControllerBase>();
  m_row_idx = m_ctrl->m_device.m_spec->get_level_id("Row") - 1;
  m_num_rows_per_bank = m_ctrl->m_device.m_spec->get_level_size("Row");

  // Sanity check: reservation can't exceed (or equal) the bank's row count
  // — there'd be no room left for any workload row.
  if (m_reserved_rows_per_bank >= m_num_rows_per_bank) {
    throw std::runtime_error(
        "RITAddrMapper: reserved_rows_per_bank (" +
        std::to_string(m_reserved_rows_per_bank) +
        ") must be less than num_rows_per_bank (" +
        std::to_string(m_num_rows_per_bank) + ")");
  }

  m_base_mapper = create_base_mapper();
}

IAddrMapper* RITAddrMapper::create_base_mapper() {
  // RAMULATOR_CHILD: addr_mapper
  ConfigNode base_node = m_config["addr_mapper"];
  if (!base_node) {
    throw std::runtime_error("RITAddrMapper: nested 'addr_mapper' is required");
  }
  std::string base_impl = base_node["impl"].as<std::string>("");
  if (base_impl.empty()) {
    throw std::runtime_error("RITAddrMapper: nested 'addr_mapper.impl' is required");
  }

  ConfigNode wrapper(ConfigNode::Map{{"addr_mapper", base_node}});
  Implementation* impl =
      Factory::create_implementation("addr_mapper", base_impl, wrapper, m_ctrl);
  auto* base_mapper = dynamic_cast<IAddrMapper*>(impl);
  if (!base_mapper) {
    throw std::runtime_error(
        "RITAddrMapper: failed to construct nested addr_mapper '" + base_impl + "'");
  }
  return base_mapper;
}

void RITAddrMapper::apply(Request& req) {
  // 1) Delegate flat-address decomposition to the wrapped mapper.
  m_base_mapper->apply(req);

  // 2) RIT lookup — redirect logical row to its swap target if any.
  if (m_initialized) {
    int bank = m_ctrl->m_device.get_flat_bank_id(req.addr_vec);
    int row = req.addr_vec[m_row_idx + 1];
    auto it = m_rit[bank].find(row);
    if (it != m_rit[bank].end()) {
      req.addr_vec[m_row_idx + 1] = it->second.swapped_row;
    }
  }

  // 3) Reservation shift — workload rows skip the reserved low rows.
  // Bounds check: a workload that uses rows near num_rows-1 would land
  // out of range after the shift.
  if (m_reserved_rows_per_bank > 0) {
    int& row = req.addr_vec[m_row_idx + 1];
    int shifted = row + m_reserved_rows_per_bank;
    if (shifted >= m_num_rows_per_bank) {
      throw std::runtime_error(
          "RITAddrMapper: workload row " + std::to_string(row) +
          " + reserved_rows_per_bank " + std::to_string(m_reserved_rows_per_bank) +
          " = " + std::to_string(shifted) +
          " exceeds num_rows_per_bank " + std::to_string(m_num_rows_per_bank) +
          ". Reduce workload row range or shrink the reserved region.");
    }
    row = shifted;
  }
}

void RITAddrMapper::init_rit(int num_banks, int max_entries_per_bank) {
  m_rit.assign(num_banks, {});
  m_max_entries_per_bank = max_entries_per_bank;
  m_initialized = true;
}

int RITAddrMapper::check_rit(int bank, int row) const {
  auto it = m_rit[bank].find(row);
  return (it == m_rit[bank].end()) ? -1 : it->second.swapped_row;
}

bool RITAddrMapper::is_rit_locked(int bank, int row) const {
  auto it = m_rit[bank].find(row);
  return (it != m_rit[bank].end()) && it->second.locked;
}

bool RITAddrMapper::is_rit_full(int bank) const {
  // Each swap occupies two entries (src, dst) — budget is per swap-pair.
  return (int)m_rit[bank].size() >= m_max_entries_per_bank * 2;
}

void RITAddrMapper::insert_entry(int bank, int src_row, int dst_row) {
  m_rit[bank][src_row] = RITEntry{dst_row, /*locked=*/true};
  m_rit[bank][dst_row] = RITEntry{src_row, /*locked=*/true};
}

void RITAddrMapper::remove_entry(int bank, int src_row, int dst_row) {
  m_rit[bank].erase(src_row);
  m_rit[bank].erase(dst_row);
}

std::pair<int, int> RITAddrMapper::get_unswap_pair(
    int bank, const std::unordered_map<int, int>& hrt) const {
  for (auto& [src, entry] : m_rit[bank]) {
    if (hrt.find(src) == hrt.end()) {
      return {src, entry.swapped_row};
    }
  }
  auto it = m_rit[bank].begin();
  return {it->first, it->second.swapped_row};
}

void RITAddrMapper::rit_unlock() {
  for (auto& bank : m_rit) {
    for (auto& [row, entry] : bank) entry.locked = false;
  }
}

void RITAddrMapper::dump_rit(int bank) const {
  std::cout << "RIT[" << bank << "].size(): " << m_rit[bank].size() << std::endl;
  for (auto& [row, entry] : m_rit[bank]) {
    std::cout << "  " << row << " -> " << entry.swapped_row
              << " (locked=" << entry.locked << ")" << std::endl;
  }
}

}  // namespace Ramulator
