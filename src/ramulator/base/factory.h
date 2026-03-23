#ifndef RAMULATOR_BASE_FACTORY_H
#define RAMULATOR_BASE_FACTORY_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/config_node.h"
#include "ramulator/base/debug.h"
#include "ramulator/base/logger.h"
#include "ramulator/base/type.h"

namespace Ramulator {

class Implementation;
class IFrontEnd;
class IMemorySystem;

// Self-registering component factory. Interfaces and implementations register
// themselves at static init time via RAMULATOR_REGISTER_* macros; at runtime
// the factory instantiates components by name from config.
class Factory {
  using Constructor_t = std::function<Implementation*(const ConfigNode&, Implementation*)>;
  struct InterfaceInfo {
    std::string name;

    struct ImplementationInfo {
      std::string name;
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
  static bool register_interface(std::string ifce_name);

  static bool query_interface(std::string ifce_name);

  /**
   * @brief     Registers an implementation class of an interface to the registry.
   *
   * @return true   Registration sucessful.
   * @return false  Registration failed, is another interface with the same name already exists?
   */
  static bool register_implementation(std::string ifce_name, std::string impl_name, const Constructor_t& cstr);

  /**
   * @brief     Construct an implementation object given the name of the implementation.
   *
   */
  static Implementation* create_implementation(std::string ifce_name, std::string impl_name, const ConfigNode& config,
                                               Implementation* parent);
  static Implementation* create_implementation(std::string ifce_name, const ConfigNode& config, Implementation* parent);

  static IFrontEnd* create_frontend(const ConfigNode& config);
  static IMemorySystem* create_memory_system(const ConfigNode& config);

  // Hide all constructors
 public:
  Factory() = delete;
  Factory(const Factory&) = delete;
  void operator=(const Factory&) = delete;
  Factory(Factory&&) = delete;
};

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_FACTORY_H