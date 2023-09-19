#ifndef     RAMULATOR_TEST_TEST_IFCE_H
#define     RAMULATOR_TEST_TEST_IFCE_H

#include "base/base.h"


namespace Ramulator {

class TestIfce {
  RAMULATOR_REGISTER_INTERFACE(TestIfce, "TestIfce", "A test example of an interface class.")
};

}        // namespace Ramulator

#endif   // RAMULATOR_TEST_TEST_IFCE_H