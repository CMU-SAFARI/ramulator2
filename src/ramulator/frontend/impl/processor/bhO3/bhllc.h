#ifndef RAMULATOR_FRONTEND_PROCESSOR_BHO3_LLC_H
#define RAMULATOR_FRONTEND_PROCESSOR_BHO3_LLC_H

#include <list>
#include <unordered_map>
#include <vector>

#include "ramulator/base/debug.h"
#include "ramulator/base/request.h"
#include "ramulator/base/type.h"
#include "ramulator/memory_system/i_memory_system.h"

namespace Ramulator {

// BHO3LLC — SimpleO3LLC equivalent extended with a per-core MSHR-blacklist
// API. The BlockHammer controller calls add_blacklist(source_id) and
// set_blacklist_max_mshrs(source_id, cap) when it identifies an attacker
// core; once a source is blacklisted, the LLC's send() rejects requests
// from that source whose outstanding-MSHR count exceeds its cap.
class BHO3LLC {
  friend class BHO3;
  struct Line {
    Addr_t addr = -1;
    Addr_t tag = -1;
    bool dirty = false;
    bool ready = false;  // line in-flight indicator
  };

 private:
  const Clk_t& m_clk;
  using CacheSet_t = std::list<Line>;
  std::unordered_map<int, CacheSet_t> m_cache_sets;

  using MSHREntry_t = std::pair<Addr_t, CacheSet_t::iterator>;
  using MSHR_t = std::vector<MSHREntry_t>;
  MSHR_t m_mshrs;
  std::unordered_map<Addr_t, std::vector<Request>> m_receive_requests;

  std::list<std::pair<Clk_t, Request>> m_miss_list;
  std::list<std::pair<Clk_t, Request>> m_hit_list;

  IMemorySystem* m_memory_system;

  Logger m_logger;

  int m_latency;

  size_t m_size_bytes;
  size_t m_linesize_bytes;
  int m_associativity;
  int m_set_size;
  int m_num_mshrs;
  int m_num_cores;

  Addr_t m_index_mask;
  int m_index_offset;
  int m_tag_offset;

  // Per-core MSHR blacklist state. m_allocated_mshrs tracks current
  // outstanding misses per core; m_blacklist_max_mshrs is the cap (0 means
  // no cap); m_blacklist_status flags cores that the controller has
  // explicitly added to the blacklist.
  int m_mshr_per_core;
  std::vector<int> m_allocated_mshrs;
  std::vector<int> m_blacklist_max_mshrs;
  std::vector<bool> m_blacklist_status;

  // Stats
  int s_llc_read_access = 0;
  int s_llc_write_access = 0;
  int s_llc_read_misses = 0;
  int s_llc_write_misses = 0;
  int s_llc_eviction = 0;
  int s_llc_mshr_unavailable = 0;
  int s_llc_mshr_blacklisted = 0;

 public:
  BHO3LLC(const Clk_t& clk, int latency, int size_bytes, int linesize_bytes,
          int associativity, int num_mshrs, int num_cores);
  void connect_memory_system(IMemorySystem* memory_system) {
    m_memory_system = memory_system;
  };

  void tick();
  bool send(Request& req);
  void receive(Request& req);

  // ── BlockHammer-specific blacklist API ────────────────────────────
  int get_mshrs_per_core() const {
    return m_mshr_per_core;
  };
  void add_blacklist(int source_id);
  void erase_blacklist(int source_id);
  void set_blacklist_max_mshrs(int source_id, int max_mshr);
  int get_blacklist_max_mshrs(int source_id) const;

  // Software-style cache-line flush. Removes the line containing addr
  // from its set, generating a writeback request if the line was dirty.
  // Returns true iff a line was actually evicted.
  bool clflush(Addr_t addr);

 private:
  int get_index(Addr_t addr) {
    return (addr >> m_index_offset) & m_index_mask;
  };
  Addr_t get_tag(Addr_t addr) {
    return (addr >> m_tag_offset);
  };
  Addr_t align(Addr_t addr) {
    return (addr & ~(m_linesize_bytes - 1l));
  };

  CacheSet_t& get_set(Addr_t addr);
  CacheSet_t::iterator allocate_line(CacheSet_t& set, Addr_t addr);
  bool need_eviction(const CacheSet_t& set, Addr_t addr);
  void evict_line(CacheSet_t& set, CacheSet_t::iterator victim_it);

  CacheSet_t::iterator check_set_hit(CacheSet_t& set, Addr_t addr);
  MSHR_t::iterator check_mshr_hit(Addr_t addr);
};

}  // namespace Ramulator

#endif  // RAMULATOR_FRONTEND_PROCESSOR_BHO3_LLC_H
