#ifndef     RAMULATOR_BASE_REQUEST_H
#define     RAMULATOR_BASE_REQUEST_H

#include <vector>
#include <list>
#include <string>

#include "base/base.h"

namespace Ramulator {

struct Request { 
  Addr_t    addr = -1;
  AddrVec_t addr_vec {};

  // Basic request id convention
  // 0 = Read, 1 = Write. The device spec defines all others
  struct Type {
    enum : int {
      Read = 0, 
      Write,
    };
  };

  int type_id = -1;    // An identifier for the type of the request
  int source_id = -1;  // An identifier for where the request is coming from (e.g., which core)

  int command = -1;          // The command that need to be issued to progress the request
  int final_command = -1;    // The final command that is needed to finish the request
  bool is_stat_updated = false; // Memory controller stats

  Clk_t arrive = -1;   // Clock cycle when the request arrive at the memory controller
  Clk_t depart = -1;   // Clock cycle when the request depart the memory controller

  std::array<int, 4> scratchpad = { 0 };    // A scratchpad for the request

  std::function<void(Request&)> callback;

  void* m_payload = nullptr;    // Point to a generic payload

  Request(Addr_t addr, int type);
  Request(AddrVec_t addr_vec, int type);
  Request(Addr_t addr, int type, int source_id, std::function<void(Request&)> callback);
};


struct ReqBuffer {
  std::list<Request> buffer;
  size_t max_size = 32;


  using iterator = std::list<Request>::iterator;
  iterator begin() { return buffer.begin(); };
  iterator end() { return buffer.end(); };


  size_t size() const { return buffer.size(); }

  bool enqueue(const Request& request) {
    if (buffer.size() <= max_size) {
      buffer.push_back(request);
      return true;
    } else {
      return false;
    }
  }

  void remove(iterator it) {
    buffer.erase(it);
  }
};

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_REQUEST_H