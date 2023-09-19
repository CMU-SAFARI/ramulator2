#include <iostream>
#include "frontend/impl/processor/simpleO3/llc.h"

namespace Ramulator {

SimpleO3LLC::SimpleO3LLC(int latency, int size_bytes, int linesize_bytes, int associativity, int num_mshrs):
m_latency(latency), m_size_bytes(size_bytes), m_linesize_bytes(linesize_bytes), m_associativity(associativity), m_num_mshrs(num_mshrs) {
  m_logger = Logging::create_logger("SimpleO3LLC");

  m_set_size = m_size_bytes / (m_linesize_bytes * m_associativity);
  m_index_mask = m_set_size - 1;
  m_index_offset = calc_log2(m_linesize_bytes);
  m_tag_offset = calc_log2(m_set_size) + m_index_offset;

  DEBUG_LOG(DSIMPLEO3LLC, m_logger, "Index mask: {0:x}", m_index_mask);
  DEBUG_LOG(DSIMPLEO3LLC, m_logger, "Index offset: {}",  m_index_offset);
  DEBUG_LOG(DSIMPLEO3LLC, m_logger, "Tag offset: {}",    m_tag_offset);
};

void SimpleO3LLC::tick() {
  m_clk++;

  // Send miss requests to the memory system when LLC latency is met
  // TODO: Optimization by assuming in-order issue?
  auto it = m_miss_list.begin(); 
  while (it != m_miss_list.end()) {
    if (m_clk >= it->first) {
      if (!m_memory_system->send(it->second)) {
        it++;
      }
      else {
        it = m_miss_list.erase(it);
      }
    } else {
      it++;
    }
  }

  // call hit request callback when LLC latency is met
  it = m_hit_list.begin();
  while (it != m_hit_list.end()) {
    if (m_clk >= it->first) {
      std::vector<Request> _req_v{it->second};
      m_receive_requests[it->second.addr] = _req_v;

      it->second.callback(it->second);
      it = m_hit_list.erase(it);
    } 
    else {
      it++;
    }
  }
};

bool SimpleO3LLC::send(Request req) {
  CacheSet_t& set = get_set(req.addr);

  if (req.type_id == Request::Type::Read) {
    s_llc_read_access++;
  } else if (req.type_id == Request::Type::Write) {
    s_llc_write_access++;
  }

  if (auto line_it = check_set_hit(set, req.addr); line_it != set.end()) {
    // Hit in the set
    DEBUG_LOG(DSIMPLEO3LLC, m_logger, 
    "[Clk={}] Request Source: {}, Type: {}, Addr: {}, Index: {}, Tag: {}. Hit, will finish at Clk={}", 
    m_clk, req.source_id, req.type_id, req.addr, get_index(req.addr), get_tag(req.addr), m_clk, m_clk + m_latency
    );

    // Update the LRU status
    set.push_back({req.addr, get_tag(req.addr), line_it->dirty || (req.type_id == Request::Type::Write), true});
    set.erase(line_it);

    // Add to the hit list to callback when finished
    m_hit_list.push_back(std::make_pair(m_clk + m_latency, req));
    return true;
  } else {
    // Miss in the set
    DEBUG_LOG(DSIMPLEO3LLC, m_logger, 
    "[Clk={}] Request Source: {}, Type: {}, Addr: {}, Index: {}, Tag: {}. Miss.", 
    m_clk, req.source_id, req.type_id, req.addr, get_index(req.addr), get_tag(req.addr), m_clk, m_clk + m_latency
    );

    if (req.type_id == Request::Type::Read) {
      s_llc_read_misses++;
    } else if (req.type_id == Request::Type::Write) {
      s_llc_write_misses++;
    }

    bool dirty = (req.type_id == Request::Type::Write);
    if (req.type_id == Request::Type::Write) {
      req.type_id = Request::Type::Read;
    }

    // MSHR lookup
    auto mshr_it = check_mshr_hit(req.addr);
    if (mshr_it != m_mshrs.end()) {
      DEBUG_LOG(DSIMPLEO3LLC, m_logger,  "MSHR Hit.", m_clk);
      // Add new req to MSHR_requests
      m_receive_requests[mshr_it->first].push_back(req);

      mshr_it->second->dirty = dirty || mshr_it->second->dirty;
      return true;
    }

    // MSHR miss
    // Check if there is available MSHR entry
    if (m_mshrs.size() == m_num_mshrs) {
      DEBUG_LOG(DSIMPLEO3LLC, m_logger,  "No MSHR entry available.", m_clk);
      s_llc_mshr_unavailable++;
      return false;
    }

    // Check if there is available cache line in the set
    bool line_available = false;
    if (set.size() < m_associativity) {
      line_available = true;
    } else {
      for (const auto& line : set) {
        if (line.ready) {
          line_available = true;
        }
      }
    }
    if (!line_available) {
      DEBUG_LOG(DSIMPLEO3LLC, m_logger,  "No cache line available in the set.", m_clk);
      return false;
    }

    // Allocate a new cache line
    auto newline_it = allocate_line(set, req.addr);
    if (newline_it == set.end()) {
      // Should this happen?
      throw std::runtime_error("Failed to allocate new line when there is available entry.");
      return false;
    }
    newline_it->dirty = dirty;
    
    // Add to MSHR entries
    m_mshrs.push_back(std::make_pair(req.addr, newline_it));
    // Add Request to MSHR_requests
    std::vector<Request> _req_v{req};
    m_receive_requests[req.addr] = _req_v;

    // Add to the miss request list
    m_miss_list.push_back(std::make_pair(m_clk + m_latency, req));

    return true;
  }
};

void SimpleO3LLC::receive(Request& req) {
  auto it = std::find_if(
    m_mshrs.begin(), m_mshrs.end(),
    [&req, this](MSHREntry_t mshr_entry) { return (align(mshr_entry.first) == align(req.addr)); }
  );

  DEBUG_LOG(DSIMPLEO3LLC, m_logger, "[Clk={}] Request {} received.", m_clk, req.addr);

  if (it != m_mshrs.end()) {
    it->second->ready = true;
    m_mshrs.erase(it);
  }
};

SimpleO3LLC::CacheSet_t& SimpleO3LLC::get_set(Addr_t addr) {
  int set_index = get_index(addr);
  if (m_cache_sets.find(set_index) == m_cache_sets.end()) {
    m_cache_sets.insert(make_pair(set_index, std::list<Line>()));
  }
  return m_cache_sets[set_index];
}

SimpleO3LLC::CacheSet_t::iterator SimpleO3LLC::allocate_line(CacheSet_t& set, Addr_t addr) {
  // Check if we need to evict any line
  if (need_eviction(set, addr)) {
    // Get a victim to evict
    auto victim = std::find_if(set.begin(), set.end(), [this](Line line) { return line.ready; });
    if (victim == set.end())
      return victim;  // doesn't exist a line that's already unlocked in each level
    evict_line(set, victim);
  }

  // Allocate new cache line and return an iterator to it
  set.push_back({addr, get_tag(addr)});
  return --set.end();
}

bool SimpleO3LLC::need_eviction(const CacheSet_t& set, Addr_t addr) {
  if (std::find_if(set.begin(), set.end(), 
            [addr, this](Line l) { return (get_tag(addr) == l.tag); }) 
      != set.end()) {
    // Due to MSHR, the program can't reach here. Just for checking
    assert(false);
    return false;
  } 
  else {
    if (set.size() < m_associativity) {
      return false;
    } else {
      return true;
    }
  }
}

void SimpleO3LLC::evict_line(CacheSet_t& set, CacheSet_t::iterator victim_it) {
  DEBUG_LOG(DSIMPLEO3LLC, m_logger,  "Evicting {}.", victim_it->addr);
  s_llc_eviction++;

  // Generate writeback request if victim line is dirty
  if (victim_it->dirty) {
    Request writeback_req(victim_it->addr, Request::Type::Write);
    m_miss_list.push_back(std::make_pair(m_clk + m_latency, writeback_req));

    DEBUG_LOG(DSIMPLEO3LLC, m_logger,  "Writeback Request will be issued at Clk={}.", m_clk + m_latency);
  }

  set.erase(victim_it);
}


SimpleO3LLC::CacheSet_t::iterator SimpleO3LLC::check_set_hit(CacheSet_t& set, Addr_t addr) {
  auto line_it = std::find_if(set.begin(), set.end(), [addr, this](Line l){return (l.tag == get_tag(addr));});
  if (!line_it->ready) {
    return set.end();
  } else {
    return line_it;
  }
}

SimpleO3LLC::MSHR_t::iterator SimpleO3LLC::check_mshr_hit(Addr_t addr) {
  auto mshr_it =
    std::find_if(
      m_mshrs.begin(), m_mshrs.end(),
      [addr, this](MSHREntry_t mshr_entry) { return (align(mshr_entry.first) == align(addr)); }
    );
  return mshr_it;
}

void SimpleO3LLC::serialize(std::string serialization_filename) {
  std::ofstream serialization_file;
  serialization_file.open(serialization_filename, std::ios::out);

  serialization_file << "index,addr,tag,dirty" << std::endl;
  for (auto it1 = m_cache_sets.begin(); it1 != m_cache_sets.end(); it1++) {
    for (auto it2 = it1->second.begin(); it2 != it1->second.end(); it2++) {
      serialization_file << it1->first << "," << it2->addr << "," << it2->tag << "," << it2->dirty << std::endl;
    }
  }
  serialization_file.close();
}

void SimpleO3LLC::deserialize(std::string serialization_filename) {
  std::ifstream serialization_file;
  serialization_file.open(serialization_filename, std::ios::out);

  std::string file_line;
  std::getline(serialization_file, file_line); // Skip the first line, which is the header
  while (std::getline(serialization_file, file_line)) {
    std::string index_str = file_line.substr(0, file_line.find(","));
    file_line = file_line.substr(file_line.find(",") + 1);
    std::string addr_str = file_line.substr(0, file_line.find(","));
    file_line = file_line.substr(file_line.find(",") + 1);
    std::string tag_str = file_line.substr(0, file_line.find(","));
    file_line = file_line.substr(file_line.find(",") + 1);
    std::string dirty_str = file_line.substr(0, file_line.find(","));
    
    int index = std::stoi(index_str);
    Addr_t addr = std::stoll(addr_str);
    Addr_t tag = std::stoll(tag_str);
    bool dirty = std::stoi(dirty_str);
    if(m_cache_sets.find(index) == m_cache_sets.end()){
      m_cache_sets.insert({index, std::list<SimpleO3LLC::Line>()});
    }
    m_cache_sets[index].push_back({addr, tag, dirty, 1});
  }
  serialization_file.close();
}

void SimpleO3LLC::dump_llc() {
  /**
   * @brief dumps the LLC cache to the console
   * 
   */
  std::cout << "Dumping LLC" << std::endl;
  std::cout << "index,addr,tag,dirty,ready" << std::endl;
  for (auto it1 = m_cache_sets.begin(); it1 != m_cache_sets.end(); it1++) {
    for (auto it2 = it1->second.begin(); it2 != it1->second.end(); it2++) {
      std::cout << it1->first << "," << it2->addr << "," << it2->tag << "," << it2->dirty << "," << it2->ready << std::endl;
    }
  }
}

}        // namespace Ramulator