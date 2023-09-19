#include <iostream>
#include <fstream>
#include <map>
#include <list>

#include "example/example_ifce.h"
#include "base/serialization.h"

namespace Ramulator {

class ExampleImpl : public ExampleIfce, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(ExampleIfce, ExampleImpl, "ExampleImplementation", "An example of an implementation class.")

  private:
    int s;

    std::vector<float> fv;
    
    std::string s_str;
    std::vector<std::string> s_strv;


  public:
    std::string to_string() const override { return "I am an example implementation."; };
    void init() override {
      int i = param<int>("int_param").desc("Description for the int param").default_val(1);
      std::cout << i << std::endl;

      float f = param<float>("float_param").required();
      std::cout << f << std::endl;

      auto opt_i = param<int>("optional_param").desc("An optional param").optional();
      if (opt_i) {
        std::cout << opt_i.value() << std::endl;
      } else {
        std::cout << "opt_i is not provided" << std::endl;
      }


      // int gi = m_params.group("example_group").param<int>("in_group_param").default_val(3).required();
      // std::cout << gi << std::endl;

      int sgi = param_group("example_group").param<int>("int_param").default_val(65);
      std::cout << sgi << std::endl;


      register_stat(s).name("test_stat").desc("test desc");
      register_stat(fv).name("test_vec").desc("test vec desc");
      register_stat(s_str).name("test_str_stat");
      register_stat(s_strv).name("test_vec_str_stat");

      s = 999;

      s_str = "I am a string";
      s_strv = {"I", "am", "a", "vector", "of", "strings"};

      fv = {0.1, 0.2, 0.3};

      return;
      // int ggi = m_params.group("l1group").group("l2group").group("l3group").param<int>("nested_group_param").required();
      // std::cout << ggi << std::endl;


      // std::string s = m_params.param<std::string>("id");
    };
};

class ExampleImplBase : public ExampleIfce {
public:
  virtual void foo() = 0;
};

class ExampleImplDerived : public ExampleImplBase, public Implementation {
  RAMULATOR_REGISTER_IMPLEMENTATION(ExampleIfce, ExampleImplDerived, "ExampleImplDerived", "An example of an implementation class derived from a common implementation base class.")

  public:
    std::string to_string() const override { return "I am an example implementation."; };
    void init() override { return; };

    void foo() override { return; };
};

class ExampleSerializableImpl : public ExampleIfce, public Implementation, public Serializable<ExampleSerializableImpl> {
  RAMULATOR_REGISTER_IMPLEMENTATION(ExampleIfce, ExampleSerializableImpl, "ExampleSerializableImpl", "An example of an implementation class that is serializable.")
  public:
    int foo_int;
    std::string foo_str;
    std::string out_filename = "example_serializable_impl.csv";
    std::string in_filename = "example_serializable_impl.csv";
    bool serialize_on_exit = false;

    void init() override {
      foo_int = 123;
      foo_str = "I am a string";
      foo_lines[0].push_back(foo_struct(0, 0, false));
      foo_lines[3].push_back(foo_struct(20, 2, false));
      foo_lines[5].push_back(foo_struct(3, 30, true));
    }
    void init(bool deserialize_on_init) {
      if (deserialize_on_init) {
        foo_int = -1;
        foo_str = "";
        deserialize();
      } else {
        init();
        serialize_on_exit = true;
      }
    }

    ~ExampleSerializableImpl() {
      if (serialize_on_exit) {
        serialize();
      }
    }

    struct foo_struct {
      long addr;
      long tag;
      bool lock;
      foo_struct(long addr, long tag, bool lock):
          addr(addr), tag(tag), lock(lock) {}
    };
    
    std::map<int, std::list<foo_struct>> foo_lines;

    void serialize() override {
      std::ofstream out_file;
      out_file.open(out_filename);

      out_file << "index,addr,tag,lock" << std::endl;
      for (auto it = foo_lines.begin(); it != foo_lines.end(); ++it) {
        for (auto it2 = it->second.begin(); it2 != it->second.end(); ++it2) {
          out_file << it->first << "," << it2->addr << "," << it2->tag << "," << it2->lock << "," << std::endl;
        }
      }
      out_file.close();
    }

    void deserialize() override {
      std::ifstream in_file;
      in_file.open(in_filename);

      std::string line;
      std::getline(in_file, line); // Skip the first line, which is the header
      while (std::getline(in_file, line)) {
        std::string index_str = line.substr(0, line.find(","));
        line = line.substr(line.find(",") + 1);
        std::string addr_str = line.substr(0, line.find(","));
        line = line.substr(line.find(",") + 1);
        std::string tag_str = line.substr(0, line.find(","));
        line = line.substr(line.find(",") + 1);
        std::string lock_str = line.substr(0, line.find(","));
        
        int index = std::stoi(index_str);
        long addr = std::stol(addr_str);
        long tag = std::stol(tag_str);
        bool lock = std::stoi(lock_str);
        foo_lines[index].push_back(foo_struct(addr, tag, lock));
      }
      in_file.close();
    }

};


}        // namespace Ramulator