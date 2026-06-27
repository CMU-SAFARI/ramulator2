#include "ramulator/base/utils.h"

namespace Ramulator {

size_t parse_capacity_str(std::string size_str) {
  std::string suffixes[3] = {"KB", "MB", "GB"};
  for (int i = 0; i < 3; i++) {
    std::string suffix = suffixes[i];
    if (size_str.find(suffix) != std::string::npos) {
      size_t size = std::stoull(size_str);
      size = size << (10 * (i + 1));
      return size;
    }
  }
  return 0;
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
