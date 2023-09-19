#ifndef     RAMULATOR_BASE_FACTORY_H
#define     RAMULATOR_BASE_FACTORY_H

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/type.h"
#include "base/logging.h"
#include "base/debug.h"


namespace Ramulator {

DECLARE_DEBUG_FLAG(DFACTORY);
ENABLE_DEBUG_FLAG(DFACTORY);

class Implementation;
class IFrontEnd;
class IMemorySystem;

class Factory {
  using Constructor_t = std::function<Implementation* (const YAML::Node&, Implementation*)>;
  struct InterfaceInfo {
    std::string name;
    std::string desc;

    struct ImplementationInfo {
      std::string name;
      std::string desc;
      Constructor_t constructor;
    };
    Registry_t<ImplementationInfo> impls_info;
  };

  private:
    inline static Registry_t<InterfaceInfo> m_registry;

  public:
    /**
     * @brief     Registers an interface class to the registry.
     * 
     * @return true   Registration sucessful.
     * @return false  Registration failed, is another interface with the same name already exists?
     */
    static bool register_interface(std::string ifce_name, std::string ifce_desc);

    static bool query_interface(std::string ifce_name);


    /**
     * @brief     Registers an implementation class of an interface to the registry.
     * 
     * @return true   Registration sucessful.
     * @return false  Registration failed, is another interface with the same name already exists?
     */
    static bool register_implementation(std::string ifce_name, std::string impl_name, std::string impl_desc, const Constructor_t& cstr);

    /**
     * @brief     Construct an implementation object given the name of the implementation.
     * 
    */
    static Implementation* create_implementation(std::string ifce_name, std::string impl_name, const YAML::Node& config, Implementation* parent);
    static Implementation* create_implementation(std::string ifce_name, const YAML::Node& config, Implementation* parent);

    static IFrontEnd* create_frontend(const YAML::Node& config);
    static IMemorySystem* create_memory_system(const YAML::Node& config);

    /**
     * @brief     Prints all registered interfaces and classes.
     * 
     */
    static void dump();


  // Hide all constructors
  public:
    Factory() = delete;
    Factory(const Factory&) = delete;
    void operator=(const Factory&) = delete;
    Factory(Factory&&) = delete;
};



}        // namespace Ramulator

#endif      // RAMULATOR_BASE_BASE_H