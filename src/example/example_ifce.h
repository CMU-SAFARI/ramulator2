#ifndef     RAMULATOR_EXAMPLE_EXAMPLE_H
#define     RAMULATOR_EXAMPLE_EXAMPLE_H

#include "base/base.h"


namespace Ramulator {

class ExampleIfce {
  RAMULATOR_REGISTER_INTERFACE(ExampleIfce, "ExampleInterface", "An example of an interface class.")


};

}        // namespace Ramulator

#endif   // RAMULATOR_EXAMPLE_EXAMPLE_H