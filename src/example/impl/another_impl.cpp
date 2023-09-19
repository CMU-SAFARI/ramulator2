#include <iostream>
#include <fstream>
#include <map>
#include <list>

#include "example/example_ifce.h"
#include "example/impl/complicated_impl.h"

namespace Ramulator {

class AnotherImpl : public ExampleIfce, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(ExampleIfce, AnotherImpl, "AnotherImplementation", "An example of an implementation class.")

  ComplicatedImpl* m_cimpl;

  public:
    void init() override {
      m_cimpl = static_cast<ComplicatedImpl*>(Root::get_impl<ExampleIfce>("ComplicatedImpl"));
      m_cimpl->special_function();
    };
};



}        // namespace Ramulator