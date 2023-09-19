#include "base/utils.h"

#include <cstdint>

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

size_t parse_frequency_str(std::string size_str) {
  std::string suffixes[2] = {"MHz", "GHz"};
  for (int i = 0; i < 2; i++) {
    std::string suffix = suffixes[i];
    if (size_str.find(suffix) != std::string::npos) {
      size_t freq = std::stoull(size_str);
      freq = freq << (10 * i);
      return freq;
    }
  }
  return 0;
}

uint64_t JEDEC_rounding(float t_ns, int tCK_ps) {
  // Turn timing in nanosecond to picosecond
  uint64_t t_ps = t_ns * 1000;

  // Apply correction factor 974
  uint64_t nCK = ((t_ps * 1000 / tCK_ps) + 974) / 1000;
  return nCK;
}

uint64_t JEDEC_rounding_DDR5(float t_ns, int tCK_ps) {
  // Turn timing in nanosecond to picosecond
  uint64_t t_ps = t_ns * 1000;

  // Apply correction factor 997 and round up
  uint64_t nCK = ((t_ps * 997 / tCK_ps) + 1000) / 1000;
  return nCK;
}

int slice_lower_bits(uint64_t& addr, int bits) {
  int lbits = addr & ((1<<bits) - 1);
  addr >>= bits;
  return lbits;
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

}     // namespace Ramulator