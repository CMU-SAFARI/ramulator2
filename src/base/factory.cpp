#include <iostream>
#include <queue>

#include "base/factory.h"
#include "frontend/frontend.h"
#include "memory_system/memory_system.h"

namespace Ramulator {

bool Factory::register_interface(std::string ifce_name, std::string ifce_desc) {
  DEBUG_LOG(DFACTORY, Logging::get("Base"), "Registering interface {}...", ifce_name)

  if (auto it = m_registry.find(ifce_name); it == m_registry.end()) {
    m_registry[ifce_name] = {ifce_name, ifce_desc};
    return true;
  } else {
    throw InitializationError(
      "Interface class {} is already registered!", 
      ifce_name
    );
  }
  return false;
};


bool Factory::query_interface(std::string ifce_name) {
  if (auto it = m_registry.find(ifce_name); it != m_registry.end()) {
    return true;
  } else {
    return false;
  }
}


bool Factory::register_implementation(std::string ifce_name, std::string impl_name, std::string impl_desc, const Constructor_t& cstr) {
  DEBUG_LOG(DFACTORY, Logging::get("Base"), "Registering implementation {} to interface {}...", impl_name, ifce_name)

  // First we search for the interface metadata.
  if (auto ifce_it = m_registry.find(ifce_name); ifce_it != m_registry.end()) {
    auto& [_ifce_name, ifce_info] = *ifce_it;
    auto& impls_info = ifce_info.impls_info;
    // Then we search for the implementation metadata if the interface exists.
    if (auto impl_it = impls_info.find(impl_name); impl_it == impls_info.end()) {
      impls_info[impl_name] = {impl_name, impl_desc, cstr};
      return true;
    } else {
      throw InitializationError(
        "Interface class {} of implementation {} is already yet registered!", 
        ifce_name,
        impl_name
      );    
    }

  } else {
    throw InitializationError(
      "Interface class {} of implementation {} is not yet registered!", 
      ifce_name,
      impl_name
    );    
  }
  return false;
};


Implementation* Factory::create_implementation(std::string ifce_name, std::string impl_name, const YAML::Node& config, Implementation* parent) {
  DEBUG_LOG(DFACTORY, Logging::get("Base"), "Creating implementation {} of interface {}...", impl_name, ifce_name)

  if (const auto ifce_it = m_registry.find(ifce_name); ifce_it != m_registry.end()) {
    const auto& [_ifce_name, ifce_info] = *ifce_it;
    const auto& impls_info = ifce_info.impls_info;
    if (const auto impl_it = impls_info.find(impl_name); impl_it != impls_info.end()) {
      const auto& [impl_name, impl_info] = *impl_it;
      return impl_info.constructor(config[ifce_name], parent);
    } else {
      throw InitializationError(
        "Trying to create an implementation \"{}\" of interface \"{}\", but the implementation is not registered!",
        impl_name,
        ifce_name
      );    
    }

  } else {
    throw InitializationError(
      "Trying to create an implementation of interface \"{}\", but the interface is not registered!",
      ifce_name
    );    
  }
  return nullptr;
}

Implementation* Factory::create_implementation(std::string ifce_name, const YAML::Node& config, Implementation* parent) {
  DEBUG_LOG(DFACTORY, Logging::get("Base"), "Creating an implementation of interface {}...", ifce_name)

  if (!config[ifce_name]) {
    throw InitializationError("Interface {} not found in the configuration!", ifce_name); 
    return nullptr;  
  }

  if (const auto ifce_it = m_registry.find(ifce_name); ifce_it != m_registry.end()) {
    const auto& [_ifce_name, ifce_info] = *ifce_it;
    const auto& impls_info = ifce_info.impls_info;

    const YAML::Node& ifce_config = config[ifce_name];
    std::string impl_name = ifce_config["impl"].as<std::string>("");
    if (impl_name == "") {
      throw InitializationError("No implementation specified for interface {}!", ifce_name); 
      return nullptr;  
    }
    if (const auto impl_it = impls_info.find(impl_name); impl_it != impls_info.end()) {
      const auto& [impl_name, impl_info] = *impl_it;
      return impl_info.constructor(ifce_config, parent);
    } else {
      throw InitializationError(
        "Trying to create an implementation \"{}\" of interface \"{}\", but the implementation is not registered!",
        impl_name,
        ifce_name
      );    
    }

  } else {
    throw InitializationError(
      "Trying to create an implementation of interface \"{}\", but the interface is not registered!",
      ifce_name
    );    
  }
  return nullptr;
}


void Factory::dump() {
  for (const auto& [ifce_name, ifce_info] : m_registry) {
    std::cout << fmt::format("Interface \"{}\":", ifce_name) << std::endl;
    for (const auto& [impl_name, impl_info] : ifce_info.impls_info) {
      std::cout << fmt::format("    \"{}\"", impl_name) << std::endl;
    }
  }
}

IFrontEnd* Factory::create_frontend(const YAML::Node& config) {
  Implementation* impl = Factory::create_implementation(IFrontEnd::get_name(), config, nullptr);
  IFrontEnd* frontend = dynamic_cast<IFrontEnd*>(impl);
  if (frontend == nullptr) {
    throw InitializationError("Error creating the frontend!"); 
    return nullptr;  
  }

  frontend->gather_components();

  return frontend;
};

IMemorySystem* Factory::create_memory_system(const YAML::Node& config) {
  Implementation* impl = Factory::create_implementation(IMemorySystem::get_name(), config, nullptr);
  IMemorySystem* memory_system = dynamic_cast<IMemorySystem*>(impl);
  if (memory_system == nullptr) {
    throw InitializationError("Error creating the memory system!"); 
    return nullptr;  
  }

  memory_system->gather_components();

  return memory_system;
};

}        // namespace Ramulator
