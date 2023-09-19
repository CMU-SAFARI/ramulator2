#ifndef     RAMULATOR_BASE_Clocked_H
#define     RAMULATOR_BASE_Clocked_H

#include <vector>
#include <string>

#include "base/type.h"

namespace Ramulator {

/**
 * @brief    CRTP interface for all clocked objects (i.e., can be ticked)
 * 
 * @tparam   T 
 */
template<class T>
class Clocked {
  friend T;

  protected:
    Clk_t m_clk = 0;

  public:
    virtual void tick() = 0;

  public:
    Clocked() {};
};

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_Clocked_H