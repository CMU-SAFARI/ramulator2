#ifndef     RAMULATOR_CONTROLLER_ROWPOLICY_H
#define     RAMULATOR_CONTROLLER_ROWPOLICY_H

#include <vector>
#include <string>

#include "base/base.h"


namespace Ramulator {

class IRowPolicy {
  RAMULATOR_REGISTER_INTERFACE(IRowPolicy, "RowPolicy", "Row Policy Interface.");
  protected:
    IDRAMController* m_ctrl = nullptr;

  public:
    virtual void update(bool request_found, ReqBuffer::iterator& req_it) = 0;
};

}        // namespace Ramulator


#endif   // RAMULATOR_CONTROLLER_REFRESH_H