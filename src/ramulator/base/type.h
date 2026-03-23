#ifndef RAMULATOR_BASE_TYPE_H
#define RAMULATOR_BASE_TYPE_H

#include <string>
#include <unordered_map>
#include <vector>

namespace Ramulator {

using Clk_t = int64_t;               // Clock cycle
using Addr_t = int64_t;              // Plain address as seen by the OS
using AddrVec_t = std::vector<int>;  // Device address vector as is sent to the device from the controller

template <typename T>
using Registry_t = std::unordered_map<std::string, T>;

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_TYPE_H