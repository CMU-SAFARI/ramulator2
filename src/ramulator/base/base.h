#ifndef RAMULATOR_BASE_BASE_H
#define RAMULATOR_BASE_BASE_H

#include <algorithm>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "ramulator/base/config_node.h"
#include "ramulator/base/factory.h"
#include "ramulator/base/logger.h"
#include "ramulator/base/param.h"
#include "ramulator/base/request.h"
#include "ramulator/base/stats.h"
#include "ramulator/base/type.h"
#include "ramulator/base/utils.h"

namespace Ramulator {

class IFrontEnd;
class IMemorySystem;

// Base class for all components. Provides config parsing, parent-child
// hierarchy, stats collection, and logging. Subclassed by every interface
// and implementation in the factory system.
class Implementation {
  friend class Factory;
  template <class T>
  friend class TopLevel;

 protected:
  const ConfigNode m_config;

  const std::string m_ifce_name;
  const std::string m_name;
  std::string m_id;

  Implementation* m_parent;
  std::vector<std::unique_ptr<Implementation>> m_children;

  Stats m_stats;
  Logger m_logger;

 public:
  Implementation(const ConfigNode& config, std::string ifce_name, std::string name, Implementation* parent)
      : m_config(config),
        m_ifce_name(ifce_name),
        m_name(name),
        m_id(config["id"].as<std::string>("_default_id")),
        m_parent(parent),
        m_logger(Logger(name)){};

  Implementation(const ConfigNode& config, std::string ifce_name, std::string name, std::string id,
                 Implementation* parent)
      : m_config(config), m_ifce_name(ifce_name), m_name(name), m_id(id), m_parent(parent), m_logger(Logger(name)){};

  virtual ~Implementation(){};

  virtual std::string get_name() const = 0;
  virtual std::string get_ifce_name() const = 0;
  virtual void init() = 0;
  virtual void setup(IFrontEnd* frontend, IMemorySystem* memory_system) {
    return;
  };
  // Refresh derived statistics without ending the simulation or closing
  // outputs. This may be called before every stats dump.
  virtual void update_stats() {
    return;
  };
  virtual void finalize() {
    return;
  };
  virtual void reset_stats() {
    return;
  };

  template <class T>
  T* cast_parent() {
    auto* p = dynamic_cast<T*>(m_parent);
    if (!p) {
      throw std::runtime_error("Failed to cast parent to the requested type!");
    }
    return p;
  }

  template <class Interface_t>
  Interface_t* create_child_ifce() {
    return create_child<Interface_t>(m_config, "");
  }

  template <class Interface_t>
  std::vector<Interface_t*> create_child_list(bool required = true) {
    std::string ifce_name = Interface_t::get_name();
    std::string list_key = ifce_name + "s";

    ConfigNode list_node = m_config[list_key];
    if (!list_node || !list_node.is_sequence()) {
      if (required) {
        throw std::runtime_error(ifce_name + " list ('" + list_key + "') is required!");
      }
      return {};
    }

    std::vector<Interface_t*> result;
    for (const auto& entry : list_node.seq()) {
      ConfigNode child_config = entry;
      std::string impl_name = child_config["impl"].as<std::string>("");
      if (impl_name.empty()) {
        throw std::runtime_error("No impl specified for " + ifce_name + " list entry!");
      }
      ConfigNode wrapper(ConfigNode::Map{{ifce_name, child_config}});
      Implementation* impl = Factory::create_implementation(ifce_name, impl_name, wrapper, this);
      add_child(impl);
      auto* typed = dynamic_cast<Interface_t*>(impl);
      if (!typed) {
        throw std::runtime_error("Failed to cast " + impl_name + " to " + ifce_name);
      }
      result.push_back(typed);
    }
    return result;
  }

  template <typename T>
  ParamReader<T> param(std::string param_name) {
    return {m_config, std::move(param_name), m_name};
  };

  virtual void print_stats(std::ostream& os, int indent = 0) {
    std::string pad(indent, ' ');
    os << pad << get_ifce_name() << ":\n";
    print_stats_body(os, indent + 2);
    os << "\n";
  };

  ConfigNode collect_stats() const {
    ConfigNode::Map result;
    result["impl"] = ConfigNode(get_name());
    if (get_id() != "_default_id") {
      result["id"] = ConfigNode(get_id());
    }
    for (auto& [k, v] : m_stats.collect()) {
      result[k] = std::move(v);
    }

    // Count children per interface name to detect lists
    std::map<std::string, int> ifce_counts;
    for (auto& child_impl : m_children) {
      ifce_counts[child_impl->get_ifce_name()]++;
    }

    for (auto& child_impl : m_children) {
      auto key = child_impl->get_ifce_name();
      auto child_stats = child_impl->collect_stats();
      if (ifce_counts[key] > 1) {
        // Multiple children with same interface -> collect into list
        auto it = result.find(key);
        if (it == result.end()) {
          ConfigNode::Seq seq;
          seq.push_back(std::move(child_stats));
          result[key] = ConfigNode(std::move(seq));
        } else {
          it->second.push_back(std::move(child_stats));
        }
      } else {
        result[key] = std::move(child_stats);
      }
    }
    return ConfigNode(std::move(result));
  }

  std::string get_id() const {
    return m_id;
  };
  void set_id(std::string id) {
    m_id = id;
  };

  void set_parent(Implementation* parent) {
    m_parent = parent;
  };
  void add_child(Implementation* child) {
    m_children.emplace_back(child);
  };

 private:
  void print_stats_body(std::ostream& os, int indent) {
    std::string pad(indent, ' ');
    os << pad << "impl: " << get_name() << "\n";
    if (get_id() != "_default_id") {
      os << pad << "id: " << get_id() << "\n";
    }
    m_stats.print(os, indent);

    std::map<std::string, int> ifce_counts;
    std::vector<std::string> ifce_order;
    for (auto& child_impl : m_children) {
      const auto& key = child_impl->get_ifce_name();
      if (ifce_counts[key] == 0) {
        ifce_order.push_back(key);
      }
      ifce_counts[key]++;
    }

    for (const auto& key : ifce_order) {
      if (ifce_counts[key] == 1) {
        auto& child_impl = *std::find_if(
            m_children.begin(), m_children.end(),
            [&key](const auto& child) { return child->get_ifce_name() == key; });
        if (!child_impl->m_stats.empty()) {
          os << "\n";
        }
        child_impl->print_stats(os, indent);
      } else {
        os << "\n";
        os << pad << key << ":\n";
        for (auto& child_impl : m_children) {
          if (child_impl->get_ifce_name() != key) {
            continue;
          }
          os << pad << "  -\n";
          child_impl->print_stats_body(os, indent + 4);
          os << "\n";
        }
      }
    }
  };

  template <class Interface_t>
  Interface_t* create_child(const ConfigNode& config, std::string desired_impl_name) {
    std::string ifce_name = Interface_t::get_name();

    if (!Factory::query_interface(ifce_name)) {
      throw std::runtime_error("Interface " + ifce_name + " is not registered!");
    }

    ConfigNode child_config = config[ifce_name];
    if (!child_config) {
      throw std::runtime_error("Interface " + ifce_name + " is not found in the configuration!");
    }

    std::string impl_name = child_config["impl"].as<std::string>("");
    if (impl_name.empty()) {
      throw std::runtime_error("No implementation specified for interface " + ifce_name + "!");
    }
    if (!desired_impl_name.empty() && desired_impl_name != impl_name) {
      throw std::runtime_error("Specified implementation " + impl_name + " is different from the desired " +
                               desired_impl_name + "!");
    }

    Implementation* impl = Factory::create_implementation(ifce_name, impl_name, config, this);
    add_child(impl);

    auto* result = dynamic_cast<Interface_t*>(impl);
    if (!result) {
      throw std::runtime_error("Could not convert a pointer to " + impl_name + " to a pointer to " + ifce_name + "!");
    }
    return result;
  }
};

#define RAMULATOR_REGISTER_INTERFACE(_ifce_class, _name) \
 public:                                                 \
  virtual ~_ifce_class() = default;                      \
  Implementation* m_impl = nullptr;                      \
  static std::string get_name() {                        \
    return _name;                                        \
  };                                                     \
                                                         \
 protected:                                              \
  inline static const std::string m_ifce_name = _name;   \
  static inline bool registered = Factory::register_interface(_name);

#define RAMULATOR_REGISTER_IMPLEMENTATION(_ifce_class, _impl_class, _name)                      \
 public:                                                                                        \
  inline static const std::string m_ifce_name = _ifce_class::get_name();                        \
  inline static const std::string m_name = _name;                                               \
                                                                                                \
 protected:                                                                                     \
  std::string get_name() const override {                                                       \
    return _name;                                                                               \
  };                                                                                            \
  std::string get_ifce_name() const override {                                                  \
    return _ifce_class::get_name();                                                             \
  };                                                                                            \
  _impl_class(const ConfigNode& config, Implementation* parent)                                 \
      : Implementation(config, _ifce_class::get_name(), _name, parent) {                        \
    _ifce_class::m_impl = this;                                                                 \
    init();                                                                                     \
  };                                                                                            \
  static Implementation* make_##_impl_class(const ConfigNode& config, Implementation* parent) { \
    return new _impl_class(config, parent);                                                     \
  };                                                                                            \
  static inline bool registered = Factory::register_implementation(_ifce_class::get_name(), _name, make_##_impl_class);

#define RAMULATOR_REGISTER_IMPLEMENTATION_DERIVED(_ifce_class, _impl_class, _base_class, _name) \
 public:                                                                                        \
  inline static const std::string m_ifce_name = _ifce_class::get_name();                        \
  inline static const std::string m_name = _name;                                               \
                                                                                                \
 protected:                                                                                     \
  std::string get_name() const override {                                                       \
    return _name;                                                                               \
  };                                                                                            \
  std::string get_ifce_name() const override {                                                  \
    return _ifce_class::get_name();                                                             \
  };                                                                                            \
  _impl_class(const ConfigNode& config, Implementation* parent) : _base_class(config, parent) { \
    _ifce_class::m_impl = this;                                                                 \
    init();                                                                                     \
  };                                                                                            \
  static Implementation* make_##_impl_class(const ConfigNode& config, Implementation* parent) { \
    return new _impl_class(config, parent);                                                     \
  };                                                                                            \
  static inline bool registered = Factory::register_implementation(_ifce_class::get_name(), _name, make_##_impl_class);

template <class T>
class TopLevel {
 protected:
  std::vector<Implementation*> m_components;

 public:
  void gather_components() {
    T* derived = static_cast<T*>(this);
    Implementation* impl = dynamic_cast<Implementation*>(derived);
    if (impl == nullptr) {
      throw std::runtime_error("Error converting a frontend interface to an implementation!");
    }
    std::queue<Implementation*> queue;
    for (auto& child : impl->m_children) {
      queue.push(child.get());
      derived->m_components.push_back(child.get());
    }

    while (!queue.empty()) {
      Implementation* curr = queue.front();
      queue.pop();
      for (auto& child : curr->m_children) {
        queue.push(child.get());
        derived->m_components.push_back(child.get());
      }
    }
  }

 private:
  TopLevel(){};
  friend T;
};

}  // namespace Ramulator

// Child interface creation macro — both creates the child AND serves as codegen marker.
// Codegen parses these to auto-generate Child("...") descriptors in Python wrappers.
//
// Usage:
//   RAMULATOR_CREATE_CHILD(m_translation, ITranslation);
//   RAMULATOR_CREATE_CHILD(m_addr_mapper, IAddrMapper);
#define RAMULATOR_CREATE_CHILD(var, interface, ...) var = create_child_ifce<interface>(__VA_ARGS__)

// Child list creation macro — creates a list of children AND serves as codegen marker.
// Codegen parses these to auto-generate ChildList("...") descriptors in Python wrappers.
//
// Usage:
//   RAMULATOR_CREATE_CHILD_LIST(m_controllers, IController);
#define RAMULATOR_CREATE_CHILD_LIST(var, interface) var = create_child_list<interface>()

// Optional child list — returns empty vector when the config key is absent.
// Same codegen behavior as RAMULATOR_CREATE_CHILD_LIST (generates ChildList descriptor).
//
// Usage:
//   RAMULATOR_CREATE_OPTIONAL_CHILD_LIST(m_plugins, IControllerPlugin);
#define RAMULATOR_CREATE_OPTIONAL_CHILD_LIST(var, interface) var = create_child_list<interface>(false)

#endif  // RAMULATOR_BASE_BASE_H
