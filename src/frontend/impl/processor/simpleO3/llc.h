#ifndef     RAMULATOR_FRONTEND_PROCESSOR_SIMPLEO3_LLC_H
#define     RAMULATOR_FRONTEND_PROCESSOR_SIMPLEO3_LLC_H

#include <vector>
#include <list>
#include <unordered_map>
#include <iostream>
#include <fstream>

#include "base/clocked.h"
#include "base/debug.h"
#include "base/type.h"
#include "base/request.h"
#include "memory_system/memory_system.h"

namespace Ramulator {

DECLARE_DEBUG_FLAG(DSIMPLEO3LLC);
// ENABLE_DEBUG_FLAG(DSIMPLEO3LLC);

class SimpleO3LLC : public Clocked<SimpleO3LLC> {
  friend class SimpleO3;
  struct Line {
    Addr_t addr = -1;
    Addr_t tag = -1;
    bool dirty = false;
    bool ready = false;   // Whether this line is ready (i.e., is still inflight?)
  };

  private:
    using CacheSet_t = std::list<Line>;   // LRU queue for the set. The head of the list is the least-recently-used way.
    std::unordered_map<int, CacheSet_t> m_cache_sets;
    
    using MSHREntry_t = std::pair<Addr_t, CacheSet_t::iterator>;
    using MSHR_t = std::vector<MSHREntry_t>;
    MSHR_t m_mshrs;
    std::unordered_map<Addr_t, std::vector<Request>> m_receive_requests;

    // Request that miss in the LLC with the clock cycle (current cycle + llc latency) that they 
    // should be sent to the memory system
    std::list<std::pair<Clk_t, Request>> m_miss_list;

    // Request that hit in the LLC with the clock cycle (current cycle + llc latency) that they 
    // should be sent back to the core (calls the callback)
    std::list<std::pair<Clk_t, Request>> m_hit_list;

    IMemorySystem* m_memory_system;

    Logger_t m_logger;


  public:
    int m_latency;

    size_t m_size_bytes;
    size_t m_linesize_bytes;
    int m_associativity;
    int m_set_size;
    int m_num_mshrs;

    Addr_t m_index_mask;
    int m_index_offset;
    int m_tag_offset;


    int s_llc_read_access = 0;
    int s_llc_write_access = 0;
    int s_llc_read_misses = 0;
    int s_llc_write_misses = 0;
    int s_llc_eviction = 0;
    int s_llc_mshr_unavailable = 0;
    

  public:
    SimpleO3LLC(int latency, int size_bytes, int linesize_bytes, int associativity, int num_mshrs);
    void connect_memory_system(IMemorySystem* memory_system) { m_memory_system = memory_system; };
    
    void tick();
    bool send(Request req);
    void receive(Request& req);

    void serialize(std::string serialization_filename);
    void deserialize(std::string serialization_filename);
    void dump_llc();

  private:
    int get_index(Addr_t addr)  { return (addr >> m_index_offset) & m_index_mask; };
    Addr_t get_tag(Addr_t addr) { return (addr >> m_tag_offset); };
    Addr_t align(Addr_t addr)   { return (addr & ~(m_linesize_bytes-1l)); };

    CacheSet_t& get_set(Addr_t addr);
    CacheSet_t::iterator allocate_line(CacheSet_t& set, Addr_t addr);
    bool need_eviction(const CacheSet_t& set, Addr_t addr);
    void evict_line(CacheSet_t& set, CacheSet_t::iterator victim_it);

    CacheSet_t::iterator check_set_hit(CacheSet_t& set, Addr_t addr);
    MSHR_t::iterator check_mshr_hit(Addr_t addr);
};

}        // namespace Ramulator


#endif   // RAMULATOR_FRONTEND_PROCESSOR_SIMPLEO3_LLC_H