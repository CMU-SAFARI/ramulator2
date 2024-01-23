#ifndef RAMULATOR_CONTROLLER_LIVESCHEDULER_H
#define RAMULATOR_CONTROLLER_LIVESCHEDULER_H

#include <vector>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"

namespace Ramulator {

class IBHScheduler {
  RAMULATOR_REGISTER_INTERFACE(IBHScheduler, "BHScheduler", "Memory Controller Request Scheduler");
  
  public:
    virtual void tick() = 0;
    virtual ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) = 0;
    virtual ReqBuffer::iterator get_best_request(ReqBuffer& buffer) = 0;
};

}       // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_RAMULATOR_CONTROLLER_LIVESCHEDULER_H_H