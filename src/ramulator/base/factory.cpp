#include "ramulator/base/factory.h"

#include <fmt/format.h>

#include "ramulator/frontend/i_frontend.h"
#include "ramulator/memory_system/i_memory_system.h"

namespace Ramulator {

bool Factory::register_interface(std::string ifce_name) {
  DEBUG_LOG(Logger("Base"), "Registering interface {}...", ifce_name);

  if (auto it = m_registry.find(ifce_name); it == m_registry.end()) {
    m_registry[ifce_name] = {ifce_name};
  }
  // Already registered is OK — idempotent for shared library reloads
  return true;
}

bool Factory::query_interface(std::string ifce_name) {
  return m_registry.find(ifce_name) != m_registry.end();
}

bool Factory::register_implementation(std::string ifce_name, std::string impl_name, const Constructor_t& cstr) {
  DEBUG_LOG(Logger("Base"), "Registering implementation {} to interface {}...", impl_name, ifce_name);

  auto ifce_it = m_registry.find(ifce_name);
  if (ifce_it == m_registry.end()) {
    throw std::logic_error(
        fmt::format("Interface class {} of implementation {} is not yet registered!", ifce_name, impl_name));
  }

  auto& impls_info = ifce_it->second.impls_info;
  if (impls_info.find(impl_name) == impls_info.end()) {
    impls_info[impl_name] = {impl_name, cstr};
  }
  // Already registered is OK — idempotent for shared library reloads
  return true;
}

Implementation* Factory::create_implementation(std::string ifce_name, std::string impl_name, const ConfigNode& config,
                                               Implementation* parent) {
  DEBUG_LOG(Logger("Base"), "Creating implementation {} of interface {}...", impl_name, ifce_name);

  auto ifce_it = m_registry.find(ifce_name);
  if (ifce_it == m_registry.end()) {
    throw std::logic_error(fmt::format(
        "Trying to create an implementation of interface \"{}\", but the interface is not registered!", ifce_name));
  }

  const auto& impls_info = ifce_it->second.impls_info;
  auto impl_it = impls_info.find(impl_name);
  if (impl_it == impls_info.end()) {
    throw std::logic_error(fmt::format(
        "Trying to create an implementation \"{}\" of interface \"{}\", but the implementation is not registered!",
        impl_name, ifce_name));
  }

  return impl_it->second.constructor(config[ifce_name], parent);
}

Implementation* Factory::create_implementation(std::string ifce_name, const ConfigNode& config,
                                               Implementation* parent) {
  DEBUG_LOG(Logger("Base"), "Creating an implementation of interface {}...", ifce_name);

  if (!config[ifce_name]) {
    throw std::logic_error(fmt::format("Interface {} not found in the configuration!", ifce_name));
  }

  auto ifce_it = m_registry.find(ifce_name);
  if (ifce_it == m_registry.end()) {
    throw std::logic_error(fmt::format(
        "Trying to create an implementation of interface \"{}\", but the interface is not registered!", ifce_name));
  }

  ConfigNode ifce_config = config[ifce_name];
  std::string impl_name = ifce_config["impl"].as<std::string>("");
  if (impl_name.empty()) {
    throw std::logic_error(fmt::format("No implementation specified for interface {}!", ifce_name));
  }

  const auto& impls_info = ifce_it->second.impls_info;
  auto impl_it = impls_info.find(impl_name);
  if (impl_it == impls_info.end()) {
    throw std::logic_error(fmt::format(
        "Trying to create an implementation \"{}\" of interface \"{}\", but the implementation is not registered!",
        impl_name, ifce_name));
  }

  return impl_it->second.constructor(ifce_config, parent);
}

IFrontEnd* Factory::create_frontend(const ConfigNode& config) {
  Implementation* impl = Factory::create_implementation(IFrontEnd::get_name(), config, nullptr);
  auto* frontend = dynamic_cast<IFrontEnd*>(impl);
  if (!frontend) {
    throw std::logic_error("Error creating the frontend!");
  }
  frontend->gather_components();
  return frontend;
}

IMemorySystem* Factory::create_memory_system(const ConfigNode& config) {
  Implementation* impl = Factory::create_implementation(IMemorySystem::get_name(), config, nullptr);
  auto* memory_system = dynamic_cast<IMemorySystem*>(impl);
  if (!memory_system) {
    throw std::logic_error("Error creating the memory system!");
  }
  memory_system->gather_components();
  return memory_system;
}

}  // namespace Ramulator
