#include <iostream>
#include <fstream>
#include <map>
#include <list>

#include "example/example_ifce.h"
#include "base/serialization.h"

namespace Ramulator {

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