#include "test/test_ifce.h"

namespace Ramulator {


class TestImpl2 : public TestIfce, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(TestIfce, TestImpl2, "TestImpl2", "A test example of an implementation class.")

  public:
    void init() override {
    }
};

class TestImpl : public TestIfce, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(TestIfce, TestImpl, "TestImpl", "A test example of an implementation class.")

  TestIfce* c = nullptr;
  TestImpl2* c2 = nullptr;

  public:
    void init() override {
      c = create_child_ifce<TestIfce>();
      c2 = create_child_impl<TestIfce, TestImpl2>();
      // int i = param<int>("int_param").desc("Description for the int param").default_val(1);
      // std::cout << i << std::endl;

      // float f = param<float>("float_param").required();
      // std::cout << f << std::endl;

      // auto opt_i = param<int>("optional_param").desc("An optional param").optional();
      // if (opt_i) {
      //   std::cout << opt_i.value() << std::endl;
      // } else {
      //   std::cout << "opt_i is not provided" << std::endl;
      // }

      // int sgi = param_group("example_group").param<int>("int_param").default_val(65);
      // std::cout << sgi << std::endl;      
    }
};



}        // namespace Ramulator