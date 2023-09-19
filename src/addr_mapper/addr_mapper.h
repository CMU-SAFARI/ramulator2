#ifndef RAMULATOR_ADDR_MAPPER_ADDR_MAPPER_H
#define RAMULATOR_ADDR_MAPPER_ADDR_MAPPER_H

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"
#include "dram_controller/controller.h"

namespace Ramulator {

class IAddrMapper {
  RAMULATOR_REGISTER_INTERFACE(IAddrMapper, "AddrMapper", "Memory Controller Address Mapper");

  public:
    /**
     * @brief  Applies the address mapping to a physical address and returns the DRAM address vector
     * 
     */
    virtual void apply(Request& req) = 0;   
};

}       // namespace Ramulator

#endif  // RAMULATOR_ADDR_MAPPER_ADDR_MAPPER_H