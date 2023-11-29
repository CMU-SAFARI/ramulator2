#ifndef RAMULATOR_BASE_UTILS_H
#define RAMULATOR_BASE_UTILS_H

#include <string>
#include <vector>
#include <cstdint>

namespace Ramulator {

/************************************************
 *     Utility Functions for Parsing Configs
 ***********************************************/   

/**
 * @brief    Parse capacity strings (e.g., KB, MB) into the number of bytes
 *
 * @param    size_str       A capacity string (e.g., "8KB", "64MB").
 * @return   size_t         The number of bytes.
 */
size_t parse_capacity_str(std::string size_str);

/**
 * @brief    Parse frequency strings (e.g., MHz, GHz) into MHz
 *
 * @param    size_str       A capacity string (e.g., "4GHz", "3500MHz").
 * @return   size_t         The number of bytes.
 */
size_t parse_frequency_str(std::string size_str);

/**
 * @brief Convert a timing constraint in nanoseconds into number of cycles according to JEDEC convention.
 * 
 * @param t_ns      Timing constraint in nanoseconds
 * @param tCK_ps    Clock cycle in picoseconds
 * @return uint64_t Number of cycles
 */
uint64_t JEDEC_rounding(float t_ns, int tCK_ps);


/**
 * @brief Convert a timing constraint in nanoseconds into number of cycles according to JEDEC DDR5 convention.
 * 
 * @param t_ns      Timing constraint in nanoseconds
 * @param tCK_ps    Clock cycle in picoseconds
 * @return uint64_t Number of cycles
 */
uint64_t JEDEC_rounding_DDR5(float t_ns, int tCK_ps);


/************************************************
 *       Bitwise Operations for Integers
 ***********************************************/   

/**
 * @brief Calculate how many bits are needed to store val
 * 
 * @tparam Integral_t 
 * @param val 
 * @return Integral_t 
 */
template <typename Integral_t>
Integral_t calc_log2(Integral_t val) {
  static_assert(std::is_integral_v<Integral_t>, "Only integral types are allowed for bitwise operations!");

  Integral_t n = 0;
  while ((val >>= 1)) {
    n ++;
  }
  return n;
};

/**
 * @brief Slice the lest significant num_bits from addr and return these bits. The originial addr value is modified.
 * 
 * @tparam Integral_t 
 * @param addr 
 * @param num_bits 
 * @return Integral_t 
 */
template <typename Integral_t>
Integral_t slice_lower_bits(Integral_t& addr, int num_bits) {
  static_assert(std::is_integral_v<Integral_t>, "Only integral types are allowed for bitwise operations!");

  Integral_t lbits = addr & ((1<<num_bits) - 1);
  addr >>= num_bits;
  return lbits;
};


/************************************************
 *                Tokenization
 ***********************************************/   
void tokenize(std::vector<std::string>& tokens, std::string line, std::string delim);

}           // namespace Ramulator

#endif      // RAMULATOR_BASE_UTILS_H