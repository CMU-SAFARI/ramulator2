#ifndef RAMULATOR_BASE_UTILS_H
#define RAMULATOR_BASE_UTILS_H

#include <cstdint>
#include <string>
#include <vector>

namespace Ramulator {

/**
 * @brief    Parse capacity strings (e.g., KB, MB) into the number of bytes
 */
size_t parse_capacity_str(std::string size_str);

/**
 * @brief Calculate how many bits are needed to store val
 */
template <typename Integral_t>
Integral_t calc_log2(Integral_t val) {
  static_assert(std::is_integral_v<Integral_t>, "Only integral types are allowed for bitwise operations!");

  Integral_t n = 0;
  while ((val >>= 1)) {
    n++;
  }
  return n;
};

/**
 * @brief Slice the least significant num_bits from addr and return these bits. The original addr value is modified.
 */
template <typename Integral_t>
Integral_t slice_lower_bits(Integral_t& addr, int num_bits) {
  static_assert(std::is_integral_v<Integral_t>, "Only integral types are allowed for bitwise operations!");

  // The mask literal must be widened to Integral_t before shifting; the
  // plain literal `1` is `int`, so `1 << num_bits` is undefined for
  // num_bits >= 31 on platforms with 32-bit int, even when the destination
  // is a 64-bit Addr_t. In current Ramulator configs num_bits stays small
  // (per-level address bits), but the address mappers slice from Addr_t
  // (int64_t) and a forward-compatible mask is essentially free.
  Integral_t lbits = addr & ((static_cast<Integral_t>(1) << num_bits) - 1);
  addr >>= num_bits;
  return lbits;
};

void tokenize(std::vector<std::string>& tokens, std::string line, std::string delim);

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_UTILS_H
