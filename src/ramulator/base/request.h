#ifndef RAMULATOR_BASE_REQUEST_H
#define RAMULATOR_BASE_REQUEST_H

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "ramulator/base/type.h"

namespace Ramulator {

struct Request {
  Addr_t addr = -1;
  Addr_t intra_channel_addr = -1;  // Flat address with channel bits stripped
  AddrVec_t addr_vec{};

  // Universal built-in external request types — always Read = 0, Write = 1.
  // Additional non-negative ids may exist as metadata for future extensions.
  struct Type {
    enum : int { Read = 0, Write = 1 };
  };

  int type_id = -1;    // Request type. -1 is the convention for internal maintenance/direct-command requests.
  int source_id = -1;  // Source identifier (e.g., which core)

  int size_bytes = -1;     // Request size in bytes. Must be set explicitly by the frontend.

  int command = -1;        // Current command to issue to progress the request
  int final_command = -1;  // Terminal command needed to complete the request
  bool is_stat_updated = false;

  Clk_t arrive = -1;  // Clock cycle when the request arrives at the memory controller
  Clk_t depart = -1;  // Clock cycle when the request departs the memory controller

  std::function<void(Request&)> callback;

  // Tag type to disambiguate the internal-command constructor from the type_id one.
  struct Cmd_t {};
  static constexpr Cmd_t Cmd{};

  Request() = default;
  Request(Addr_t addr, int type);
  Request(AddrVec_t addr_vec, int type);
  Request(Addr_t addr, int type, int source_id, std::function<void(Request&)> callback);
  Request(AddrVec_t addr_vec, Cmd_t, int final_cmd);  // internal commands (refresh, row close, etc.)
};

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_REQUEST_H
