#include "ramulator/base/utils.h"

#include <stdexcept>

namespace Ramulator {

size_t parse_capacity_str(std::string size_str) {
  // Longest suffix first ("TB" before "B", "KB"/"MB"/"GB" before "B")
  // so a trailing "B" doesn't shadow the binary-prefixed forms.
  const struct {
    const char* suffix;
    int shift;
  } units[] = {
      {"TB", 40},
      {"GB", 30},
      {"MB", 20},
      {"KB", 10},
      {"B", 0},
  };

  for (const auto& u : units) {
    if (size_str.find(u.suffix) != std::string::npos) {
      try {
        size_t size = std::stoull(size_str);
        return size << u.shift;
      } catch (const std::exception& e) {
        throw std::runtime_error("parse_capacity_str: failed to parse number from '" + size_str +
                                 "': " + e.what());
      }
    }
  }

  throw std::runtime_error("parse_capacity_str: unrecognized capacity '" + size_str +
                           "' (expected one of B / KB / MB / GB / TB suffix, "
                           "e.g. '64B', '2MB', '4GB')");
}

void tokenize(std::vector<std::string>& tokens, std::string line, std::string delim) {
  size_t pos = 0;
  size_t last_pos = 0;
  while ((pos = line.find(delim, last_pos)) != std::string::npos) {
    tokens.push_back(line.substr(last_pos, pos - last_pos));
    last_pos = pos + 1;
  }
  tokens.push_back(line.substr(last_pos));
}

}  // namespace Ramulator
