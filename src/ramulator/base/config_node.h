#ifndef RAMULATOR_BASE_CONFIG_NODE_H
#define RAMULATOR_BASE_CONFIG_NODE_H

#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "ramulator/base/type.h"

namespace Ramulator {

class ConfigNode {
 public:
  using Map = std::map<std::string, ConfigNode>;
  using Seq = std::vector<ConfigNode>;
  using Scalar = std::string;

 private:
  std::variant<std::monostate, Scalar, Map, Seq> m_data;

 public:
  // Constructors
  ConfigNode() = default;
  ConfigNode(const char* val) : m_data(Scalar(val)) {
  }
  ConfigNode(Scalar val) : m_data(std::move(val)) {
  }
  ConfigNode(Map map) : m_data(std::move(map)) {
  }
  ConfigNode(Seq seq) : m_data(std::move(seq)) {
  }
  ConfigNode(int val) : m_data(std::to_string(val)) {
  }
  ConfigNode(unsigned val) : m_data(std::to_string(val)) {
  }
  ConfigNode(long val) : m_data(std::to_string(val)) {
  }
  ConfigNode(unsigned long val) : m_data(std::to_string(val)) {
  }
  ConfigNode(long long val) : m_data(std::to_string(val)) {
  }
  ConfigNode(unsigned long long val) : m_data(std::to_string(val)) {
  }
  ConfigNode(float val) : m_data(std::to_string(val)) {
  }
  ConfigNode(double val) : m_data(std::to_string(val)) {
  }
  ConfigNode(bool val) : m_data(Scalar(val ? "true" : "false")) {
  }

  // Navigation — returns null ConfigNode if key doesn't exist
  ConfigNode operator[](const std::string& key) const {
    if (auto* m = std::get_if<Map>(&m_data)) {
      auto it = m->find(key);
      if (it != m->end()) {
        return it->second;
      }
    }
    return ConfigNode{};  // null
  }

  // Existence check — true if not monostate (null/undefined)
  explicit operator bool() const {
    return !std::holds_alternative<std::monostate>(m_data);
  }

  // Type queries
  bool is_map() const {
    return std::holds_alternative<Map>(m_data);
  }
  bool is_sequence() const {
    return std::holds_alternative<Seq>(m_data);
  }
  bool is_scalar() const {
    return std::holds_alternative<Scalar>(m_data);
  }
  bool is_null() const {
    return std::holds_alternative<std::monostate>(m_data);
  }

  // Accessors (const references for iteration)
  const Map& map() const {
    if (auto* m = std::get_if<Map>(&m_data)) {
      return *m;
    }
    throw std::runtime_error("ConfigNode is not a map");
  }

  const Seq& seq() const {
    if (auto* s = std::get_if<Seq>(&m_data)) {
      return *s;
    }
    throw std::runtime_error("ConfigNode is not a sequence");
  }

  const Scalar& scalar() const {
    if (auto* s = std::get_if<Scalar>(&m_data)) {
      return *s;
    }
    throw std::runtime_error("ConfigNode is not a scalar");
  }

  size_t size() const {
    if (auto* m = std::get_if<Map>(&m_data)) {
      return m->size();
    }
    if (auto* s = std::get_if<Seq>(&m_data)) {
      return s->size();
    }
    return 0;
  }

  // Type extraction — throws on null or conversion failure
  template <typename T>
  T as() const {
    if (is_null()) {
      throw std::runtime_error("ConfigNode: cannot convert null node");
    }
    return _convert<T>();
  }

  // Type extraction with fallback — returns fallback if null
  template <typename T>
  T as(const T& fallback) const {
    if (is_null()) {
      return fallback;
    }
    return _convert<T>();
  }

  // Mutable map access (for building config nodes programmatically)
  void set(const std::string& key, ConfigNode value) {
    if (!is_map() && is_null()) {
      m_data = Map{};
    }
    if (auto* m = std::get_if<Map>(&m_data)) {
      (*m)[key] = std::move(value);
    } else {
      throw std::runtime_error("ConfigNode: cannot set key on non-map node");
    }
  }

  void push_back(ConfigNode value) {
    if (!is_sequence() && is_null()) {
      m_data = Seq{};
    }
    if (auto* s = std::get_if<Seq>(&m_data)) {
      s->push_back(std::move(value));
    } else {
      throw std::runtime_error("ConfigNode: cannot push_back on non-sequence node");
    }
  }

 private:
  template <typename T>
  T _convert() const;
};

// Specializations for _convert
template <>
inline std::string ConfigNode::_convert<std::string>() const {
  return scalar();
}

template <>
inline int ConfigNode::_convert<int>() const {
  return std::stoi(scalar());
}

template <>
inline unsigned ConfigNode::_convert<unsigned>() const {
  return static_cast<unsigned>(std::stoul(scalar()));
}

template <>
inline long ConfigNode::_convert<long>() const {
  return std::stol(scalar());
}

template <>
inline unsigned long ConfigNode::_convert<unsigned long>() const {
  return std::stoul(scalar());
}

template <>
inline long long ConfigNode::_convert<long long>() const {
  return std::stoll(scalar());
}

template <>
inline unsigned long long ConfigNode::_convert<unsigned long long>() const {
  return std::stoull(scalar());
}

template <>
inline float ConfigNode::_convert<float>() const {
  return std::stof(scalar());
}

template <>
inline double ConfigNode::_convert<double>() const {
  return std::stod(scalar());
}

template <>
inline bool ConfigNode::_convert<bool>() const {
  const auto& s = scalar();
  if (s == "true" || s == "1" || s == "yes") {
    return true;
  }
  if (s == "false" || s == "0" || s == "no") {
    return false;
  }
  throw std::runtime_error("ConfigNode: cannot convert '" + s + "' to bool");
}

// Addr_t may be size_t or uint64_t — handle via the underlying type
// If Addr_t is not already covered by the above specializations, add one:
template <>
inline std::vector<std::string> ConfigNode::_convert<std::vector<std::string>>() const {
  if (is_sequence()) {
    std::vector<std::string> result;
    for (const auto& item : seq()) {
      result.push_back(item.as<std::string>());
    }
    return result;
  }
  // Single scalar → single-element vector
  return {scalar()};
}

template <>
inline std::vector<int> ConfigNode::_convert<std::vector<int>>() const {
  if (is_sequence()) {
    std::vector<int> result;
    for (const auto& item : seq()) {
      result.push_back(item.as<int>());
    }
    return result;
  }
  // Single scalar → single-element vector
  return {_convert<int>()};
}

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_CONFIG_NODE_H
