#include <iostream>
#include <fstream>
#include <map>
#include <list>

#include "example/example_ifce.h"
#include "example/impl/complicated_impl.h"

namespace Ramulator {

  void ComplicatedImpl::special_function() {
    std::cout << "I am special" << std::endl;
  };

}        // namespace Ramulator