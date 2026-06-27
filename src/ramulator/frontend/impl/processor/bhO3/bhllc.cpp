#include "ramulator/frontend/impl/processor/bhO3/bhllc.h"

#include <algorithm>
#include <cassert>

#include "ramulator/base/utils.h"

namespace Ramulator {

BHO3LLC::BHO3LLC(const Clk_t& clk, int latency, int size_bytes, int linesize_bytes,
                 int associativity, int num_mshrs, int num_cores)
    : m_clk(clk),
      m_latency(latency),
      m_size_bytes(size_bytes),
      m_linesize_bytes(linesize_bytes),
      m_associativity(associativity),
      m_num_mshrs(num_mshrs),
      m_num_cores(num_cores) {
  m_logger = Logger("BHO3LLC");

  m_set_size = m_size_bytes / (m_linesize_bytes * m_associativity);
  m_index_mask = m_set_size - 1;
  m_index_offset = calc_log2(m_linesize_bytes);
  m_tag_offset = calc_log2(m_set_size) + m_index_offset;

  // Per-core MSHR budget — cores share m_num_mshrs equally by default; the
  // BlockHammer controller can override the cap for blacklisted cores via
  // set_blacklist_max_mshrs().
  m_mshr_per_core = (m_num_cores > 0) ? (m_num_mshrs / m_num_cores) : m_num_mshrs;
  m_allocated_mshrs.assign(m_num_cores, 0);
  m_blacklist_max_mshrs.assign(m_num_cores, 0);
  m_blacklist_status.assign(m_num_cores, false);
}

void BHO3LLC::tick() {
  auto it = m_miss_list.begin();
  while (it != m_miss_list.end()) {
    if (m_clk >= it->first) {
      if (!m_memory_system->send(it->second)) {
        it++;
      } else {
        it = m_miss_list.erase(it);
      }
    } else {
      it++;
    }
  }

  it = m_hit_list.begin();
  while (it != m_hit_list.end()) {
    if (m_clk >= it->first) {
      std::vector<Request> _req_v{it->second};
      m_receive_requests[it->second.addr] = _req_v;

      it->second.callback(it->second);
      it = m_hit_list.erase(it);
    } else {
      it++;
    }
  }
}

bool BHO3LLC::send(Request& req) {
  CacheSet_t& set = get_set(req.addr);

  if (req.type_id == Request::Type::Read) {
    s_llc_read_access++;
  } else if (req.type_id == Request::Type::Write) {
    s_llc_write_access++;
  }

  if (auto line_it = check_set_hit(set, req.addr); line_it != set.end()) {
    set.push_back({req.addr, get_tag(req.addr),
                   line_it->dirty || (req.type_id == Request::Type::Write), true});
    set.erase(line_it);
    m_hit_list.push_back(std::make_pair(m_clk + m_latency, req));
    return true;
  }

  if (req.type_id == Request::Type::Read) {
    s_llc_read_misses++;
  } else if (req.type_id == Request::Type::Write) {
    s_llc_write_misses++;
  }

  bool dirty = (req.type_id == Request::Type::Write);
  if (req.type_id == Request::Type::Write) {
    req.type_id = Request::Type::Read;
  }

  // MSHR coalescing — same-line outstanding miss
  auto mshr_it = check_mshr_hit(req.addr);
  if (mshr_it != m_mshrs.end()) {
    m_receive_requests[mshr_it->first].push_back(req);
    mshr_it->second->dirty = dirty || mshr_it->second->dirty;
    return true;
  }

  // Per-core blacklist check — if the source is blacklisted and its
  // outstanding MSHRs reach the per-core cap, reject the request. The
  // attacker's issue queue then stalls because it can't get new misses
  // through the cache, throttling its activation rate at the source.
  if (req.source_id >= 0 && req.source_id < m_num_cores &&
      m_blacklist_status[req.source_id]) {
    int cap = m_blacklist_max_mshrs[req.source_id];
    if (cap > 0 && m_allocated_mshrs[req.source_id] >= cap) {
      s_llc_mshr_blacklisted++;
      return false;
    }
  }

  if ((int)m_mshrs.size() == m_num_mshrs) {
    s_llc_mshr_unavailable++;
    return false;
  }

  bool line_available = false;
  if ((int)set.size() < m_associativity) {
    line_available = true;
  } else {
    for (const auto& line : set) {
      if (line.ready) {
        line_available = true;
      }
    }
  }
  if (!line_available) {
    return false;
  }

  auto newline_it = allocate_line(set, req.addr);
  if (newline_it == set.end()) {
    throw std::runtime_error("BHO3LLC: failed to allocate line when entry was available.");
  }
  newline_it->dirty = dirty;

  m_mshrs.push_back(std::make_pair(req.addr, newline_it));
  std::vector<Request> _req_v{req};
  m_receive_requests[req.addr] = _req_v;

  req.size_bytes = static_cast<int>(m_linesize_bytes);
  m_miss_list.push_back(std::make_pair(m_clk + m_latency, req));

  // Charge an MSHR slot to the issuing core.
  if (req.source_id >= 0 && req.source_id < m_num_cores) {
    m_allocated_mshrs[req.source_id]++;
  }

  return true;
}

void BHO3LLC::receive(Request& req) {
  auto it = std::find_if(m_mshrs.begin(), m_mshrs.end(),
                         [&req, this](MSHREntry_t mshr_entry) {
                           return (align(mshr_entry.first) == align(req.addr));
                         });

  if (it != m_mshrs.end()) {
    it->second->ready = true;
    m_mshrs.erase(it);

    // Refund the MSHR slot to the source core.
    if (req.source_id >= 0 && req.source_id < m_num_cores &&
        m_allocated_mshrs[req.source_id] > 0) {
      m_allocated_mshrs[req.source_id]--;
    }
  }
}

void BHO3LLC::add_blacklist(int source_id) {
  if (source_id < 0 || source_id >= m_num_cores) return;
  m_blacklist_status[source_id] = true;
}

void BHO3LLC::erase_blacklist(int source_id) {
  if (source_id < 0 || source_id >= m_num_cores) return;
  m_blacklist_status[source_id] = false;
}

void BHO3LLC::set_blacklist_max_mshrs(int source_id, int max_mshr) {
  if (source_id < 0 || source_id >= m_num_cores) return;
  m_blacklist_max_mshrs[source_id] = max_mshr;
}

int BHO3LLC::get_blacklist_max_mshrs(int source_id) const {
  if (source_id < 0 || source_id >= m_num_cores) return 0;
  return m_blacklist_max_mshrs[source_id];
}

bool BHO3LLC::clflush(Addr_t addr) {
  CacheSet_t& set = get_set(addr);
  auto line_it = std::find_if(set.begin(), set.end(),
                              [addr, this](Line l) { return l.tag == get_tag(addr); });
  if (line_it == set.end()) {
    return false;
  }
  // Reuse the existing eviction path so a dirty line generates a
  // proper writeback. evict_line() handles the dirty bit and stats.
  evict_line(set, line_it);
  return true;
}

BHO3LLC::CacheSet_t& BHO3LLC::get_set(Addr_t addr) {
  int set_index = get_index(addr);
  if (m_cache_sets.find(set_index) == m_cache_sets.end()) {
    m_cache_sets.insert(make_pair(set_index, std::list<Line>()));
  }
  return m_cache_sets[set_index];
}

BHO3LLC::CacheSet_t::iterator BHO3LLC::allocate_line(CacheSet_t& set, Addr_t addr) {
  if (need_eviction(set, addr)) {
    auto victim = std::find_if(set.begin(), set.end(), [](Line line) { return line.ready; });
    if (victim == set.end()) {
      return victim;
    }
    evict_line(set, victim);
  }
  set.push_back({addr, get_tag(addr)});
  return --set.end();
}

bool BHO3LLC::need_eviction(const CacheSet_t& set, Addr_t addr) {
  if (std::find_if(set.begin(), set.end(),
                   [addr, this](Line l) { return (get_tag(addr) == l.tag); }) != set.end()) {
    assert(false);
    return false;
  }
  return (int)set.size() >= m_associativity;
}

void BHO3LLC::evict_line(CacheSet_t& set, CacheSet_t::iterator victim_it) {
  s_llc_eviction++;
  if (victim_it->dirty) {
    Request writeback_req(victim_it->addr, Request::Type::Write);
    writeback_req.size_bytes = static_cast<int>(m_linesize_bytes);
    m_miss_list.push_back(std::make_pair(m_clk + m_latency, writeback_req));
  }
  set.erase(victim_it);
}

BHO3LLC::CacheSet_t::iterator BHO3LLC::check_set_hit(CacheSet_t& set, Addr_t addr) {
  auto line_it = std::find_if(set.begin(), set.end(),
                              [addr, this](Line l) { return (l.tag == get_tag(addr)); });
  if (line_it == set.end() || !line_it->ready) {
    return set.end();
  }
  return line_it;
}

BHO3LLC::MSHR_t::iterator BHO3LLC::check_mshr_hit(Addr_t addr) {
  return std::find_if(m_mshrs.begin(), m_mshrs.end(),
                      [addr, this](MSHREntry_t mshr_entry) {
                        return (align(mshr_entry.first) == align(addr));
                      });
}

}  // namespace Ramulator
