#ifndef RAMULATOR_CONTROLLER_SCHEDULER_H
#define RAMULATOR_CONTROLLER_SCHEDULER_H

#include <vector>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/base.h"

namespace Ramulator {

class IScheduler {
  RAMULATOR_REGISTER_INTERFACE(IScheduler, "Scheduler", "Memory Controller Request Scheduler");
  public:
    virtual ReqBuffer::iterator compare(ReqBuffer::iterator req1, ReqBuffer::iterator req2) = 0;

    virtual ReqBuffer::iterator get_best_request(ReqBuffer& buffer) = 0;
};

}       // namespace Ramulator

#endif  // RAMULATOR_CONTROLLER_RAMULATOR_CONTROLLER_SCHEDULER_H_H