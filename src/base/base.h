#ifndef     RAMULATOR_BASE_BASE_H
#define     RAMULATOR_BASE_BASE_H

#include <string>
#include <vector>
#include <queue>
#include <unordered_map>
#include <functional>
#include <iostream>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/type.h"
#include "base/factory.h"
#include "base/clocked.h"
#include "base/param.h"
#include "base/exception.h"
#include "base/logging.h"
#include "base/debug.h"
#include "base/request.h"
#include "base/utils.h"
#include "base/stats.h"


#ifndef uint
#define uint unsigned int
#endif

namespace Ramulator {

DECLARE_DEBUG_FLAG(DINIT);
ENABLE_DEBUG_FLAG(DINIT);

class IFrontEnd;
class IMemorySystem;

/**
 * @brief     Base class for concrete implementation of an interface in Ramulator.
 * 
 * @details
 * A common base class for concrete implementations of interfaces in Ramulator.
 * An implementation of an interface should inherit from both its corresponding interface class and this class. 
 * 
 */
class Implementation { 
  friend class Factory;
  template<class T> friend class TopLevel;

  protected:
    const YAML::Node m_config;    // Raw YAML configurations

    const std::string m_ifce_name;    // Name of the interface
    const std::string m_name;         // Name of the implementation
    const std::string m_desc;         // Description of the implementation
    std::string m_id;                 // Identifier to distinguish between multiple instances of the same implementation.

    Implementation* m_parent;                   // The pointer to the parent implementation object
    std::vector<Implementation*> m_children;    // A vector of pointers to the child implementation objects.

    Params m_params;          // The parameters of the implementation
    Stats m_stats;            // All statistics of the implementation are held here.
    Logger_t m_logger;        // Pointer to an pdlog logger.


  public:
    Implementation(const YAML::Node& config, std::string ifce_name, std::string name, std::string desc, Implementation* parent):
    m_config(config), 
    m_ifce_name(ifce_name), m_name(name), m_desc(desc), m_id(config["id"].as<std::string>("_default_id")), 
    m_parent(parent), m_params(config) {};

    Implementation(const YAML::Node& config, std::string ifce_name, std::string name, std::string desc, std::string id, Implementation* parent):
    m_config(config), 
    m_ifce_name(ifce_name), m_name(name), m_desc(desc), m_id(id), 
    m_parent(parent), m_params(config) {};

    Implementation(std::string id, Implementation* parent):
    m_id(id), m_parent(parent) {};


    virtual ~Implementation() {};

    virtual std::string get_name() const = 0;
    virtual std::string get_desc() const = 0;
    virtual std::string get_ifce_name() const = 0;
    virtual std::string to_string() const { return fmt::format("{}::to_string() placeholder", get_name()); };

    /**
     * @brief     Performs initialization of the implementation object with the supplied configuration.
     * 
     */
    virtual void init() = 0;

    /**
     * @brief     Setup the implementation that depends on other parts of the system.
     * 
     */
    virtual void setup(IFrontEnd* frontend, IMemorySystem* memory_system) { return; };

    /**
     * @brief     Things to be done when the simulation ends.
     * 
     */
    virtual void finalize() { return; };


    template<class Interface_t>
    Interface_t* cast_parent() {
      if (dynamic_cast<Interface_t*>(m_parent)) {
        return dynamic_cast<Interface_t*>(m_parent);
      } else {
        throw ConfigurationError("The parent is not an implementation of {}!", Interface_t::get_name());
        return nullptr; 
      }
    }


    template<class Interface_t>
    Interface_t* create_child_ifce(const YAML::Node& config) {
      return create_child<Interface_t>(config, "");
    }

    template<class Interface_t>
    Interface_t* create_child_ifce() {
      return create_child<Interface_t>(m_config, "");
    }


    template<class Interface_t, class Implementation_t>
    Implementation_t* create_child_impl(const YAML::Node& config) {
      std::string impl_name = Implementation_t::m_name;
      Interface_t* ifce = create_child<Interface_t>(config, impl_name);
      Implementation_t* impl = dynamic_cast<Implementation_t*>(ifce);
      if (impl == nullptr) {
        throw ConfigurationError("Failed to convert  {}!", Interface_t::get_name());
        return nullptr; 
      }
      return impl;
    }

    template<class Interface_t, class Implementation_t>
    Implementation_t* create_child_impl() {
      return create_child_impl<Interface_t, Implementation_t>(m_config);
    }


    template <typename T>
    _ParamChainer<T> param(std::string param_name) { return m_params._param<T>(param_name); };
    template <typename T>
    _ParamChainer<T> param(std::string_view param_name) { return param<T>(std::string(param_name)); };
    template <typename T>
    _ParamChainer<T> param(const char* param_name) { return param<T>(std::string(param_name)); };

    _ParamGroupChainer param_group(std::string group_name) { return m_params._group(group_name); };
  
    template <typename T>
    StatWrapper<T>& register_stat(T& val) { StatWrapper<T>* s = new StatWrapper<T>(val, *this, m_stats); return *s; };
    template <typename T>
    StatWrapper<T>& register_stat(std::vector<T>& val) { StatWrapper<T>* s = new StatWrapper<T>(val, *this, m_stats); return *s; };
    bool has_stats() { return !m_stats.is_empty(); };
    /**
     * @brief    Recursively print the stats of myself and all my childs
     * 
     */
    virtual void print_stats(YAML::Emitter& emitter) { 
      emitter << YAML::Key << get_ifce_name();
      emitter << YAML::Value;
      emitter << YAML::BeginMap;
        // Print my implementation name
        emitter << YAML::Key << "impl";
        emitter << YAML::Value << get_name();

        // Print my id if existent
        if (get_id() != "_default_id") {
          emitter << YAML::Key << "id";
          emitter << YAML::Value << get_id();
        }

        // Print all my stats
        emitter << m_stats;
        // Print all my children
        for (auto child_impl : m_children) {
          if (child_impl->has_stats()) {
            // TODO: Is this a bug in yaml-cpp that I have to emit NewLine twice?
            emitter << YAML::Newline;
            emitter << YAML::Newline;
          }
          child_impl->print_stats(emitter);
        }
      emitter << YAML::EndMap;
      emitter << YAML::Newline;
    };

    std::string get_id() const { return m_id; };
    void set_id(std::string id) { m_id = id; };

    void set_parent(Implementation* parent) { m_parent = parent; };
    void add_child(Implementation* child) { m_children.push_back(child); };

  private:
    template<class Interface_t>
    Interface_t* create_child(const YAML::Node& config, std::string desired_impl_name) {
      std::string ifce_name = Interface_t::get_name();

      // Check if the interface is registered in the factory
      bool interface_registered = Factory::query_interface(ifce_name);
      if (!interface_registered) {
        throw ConfigurationError("Interface {} is not registered!", ifce_name);
        return nullptr;
      }

      // Check if the configuraiton node contains the interface
      const YAML::Node& child_config = config[ifce_name];
      if (!child_config) {
        throw ConfigurationError("Interface {} is not found in the configuration!", ifce_name);
        return nullptr; 
      }

      // Check if an implementation is given and matches the desired
      std::string impl_name = child_config["impl"].as<std::string>("");
      if (impl_name == "") {
        throw ConfigurationError("No implementation specified for interface {}!", ifce_name);
        return nullptr; 
      }
      if (desired_impl_name != "" && desired_impl_name != impl_name) {
        throw ConfigurationError("Specified implementation {} is different from the desired {}!", impl_name, desired_impl_name);
        return nullptr; 
      }

      Implementation* impl = Factory::create_implementation(ifce_name, impl_name, config, this);
      add_child(impl);

      if (dynamic_cast<Interface_t*>(impl)) {
        return dynamic_cast<Interface_t*>(impl);
      } else {
        throw ConfigurationError("Could not convert a pointer to {} to a pointer to {}!", impl_name, ifce_name);
        return nullptr; 
      }
    }
};


/**
 * @brief     Macro for registering an interface class to the factory.
 */
#define RAMULATOR_REGISTER_INTERFACE(_ifce_class, _name, _desc) \
  public: \
  virtual ~_ifce_class() = default; \
  Implementation* m_impl = nullptr; \
  static std::string get_name() { return _name; }; \
  static std::string get_desc() { return _desc; }; \
  protected: \
  inline static const std::string m_ifce_name = _name; \
  inline static const std::string m_ifce_desc = _desc; \
  static inline bool registered = Factory::register_interface(_name, _desc);


/**
 * @brief     Macro for registering an implementation class to the factory.
 * 
 */
#define RAMULATOR_REGISTER_IMPLEMENTATION(_ifce_class, _impl_class, _name, _desc) \
  public:\
  inline static const std::string m_ifce_name = _ifce_class::get_name();\
  inline static const std::string m_name = _name;\
  inline static const std::string m_desc = _desc;\
  protected: \
  std::string get_name() const override { return _name; }; \
  std::string get_desc() const override { return _desc; }; \
  std::string get_ifce_name() const override { return _ifce_class::get_name(); }; \
  _impl_class(const YAML::Node& config, Implementation* parent) : Implementation(config, _ifce_class::get_name(), _name, _desc, parent) {\
    _ifce_class::m_impl = this; \
    m_params.set_impl_name(_name); \
    init(); \
  }; \
  static Implementation* make_ ## _impl_class(const YAML::Node& config, Implementation* parent) { return new _impl_class(config, parent); }; \
  static inline bool registered = Factory::register_implementation(_ifce_class::get_name(), _name, _desc, make_ ## _impl_class);


template <class T> 
class TopLevel {
  protected:
    std::vector<Implementation*> m_components;

  public:
    void gather_components() {
      T* derived = static_cast<T*>(this);
      Implementation* impl = dynamic_cast<Implementation*>(derived);
      if (impl == nullptr) {
        throw ConfigurationError("Error converting a frontend interface to an implementation!");
      }
      std::queue<Implementation*> queue;
      for (auto child : impl->m_children) {
        queue.push(child);
        derived->m_components.push_back(child);
      }

      while (!queue.empty()) {
        Implementation* curr = queue.front();
        queue.pop();
        for (auto child : curr->m_children) {
          queue.push(child);
          derived->m_components.push_back(child);
        }
      }
    }

    template <class Ifce_t> 
    Ifce_t* get_ifce(std::string desired_id = "") {
      for (auto component : m_components) {
        Ifce_t* target = dynamic_cast<Ifce_t*>(component);
        if (target != nullptr) {
          if (desired_id == "") {
            return target;
          } else if (component->get_id() == desired_id) {
            return target;
          }
        }
      }

      throw ConfigurationError("Cannot get Interface {}", Ifce_t::get_name());
    }

    template <class Impl_t> 
    Impl_t* get_impl(std::string desired_id = "") {
      for (auto component : m_components) {
        Impl_t* target = dynamic_cast<Impl_t*>(component);
        if (target != nullptr) {
          if (desired_id == "") {
            return target;
          } else if (component->get_id() == desired_id) {
            return target;
          }
        }
      }
      throw ConfigurationError("Cannot get Implementation {}", Impl_t::m_name());
    }

  private:
    TopLevel(){};
    friend T;
};

}        // namespace Ramulator

#endif   // RAMULATOR_BASE_BASE_H