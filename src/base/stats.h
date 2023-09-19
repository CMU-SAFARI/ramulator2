#ifndef     RAMULATOR_BASE_STATS_H
#define     RAMULATOR_BASE_STATS_H

#include <vector>
#include <string>
#include <variant>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/type.h"
#include "base/exception.h"


namespace Ramulator {

class Implementation;
class StatWrapperBase {
  public:
    virtual void emit_to(YAML::Emitter& emitter) = 0;
};

template<typename T>
class StatWrapper;

class Stats {
  template<typename T>
  friend class StatWrapper;
  friend YAML::Emitter& operator << (YAML::Emitter& emitter, const Stats& s);

  private:
    Registry_t<StatWrapperBase*> _registry;

  public:
    bool is_empty() {
      return _registry.size() == 0;
    }
};


template<typename T>
class StatWrapper : public StatWrapperBase {
  // static_assert(std::is_arithmetic_v<T>, "Only arithmetic types are allowed for Statistics!");

  private:
    std::variant<T*, std::vector<T>*> _ref;
    std::string _name;
    std::string _desc;

    const Implementation& _impl;
    Stats& _stats;

  public:
    StatWrapper(T& val, const Implementation& impl, Stats& stats) : _ref(&val), _impl(impl), _stats(stats) {};
    StatWrapper(std::vector<T>& val, const Implementation& impl, Stats& stats) : _ref(&val), _impl(impl), _stats(stats) {};

    StatWrapper& name(std::string name) { 
      _name = name; 
      if (auto it = _stats._registry.find(name); it != _stats._registry.end()) {
        throw ConfigurationError("Stat {} of implementation is already registered!", name);    
      }
      _stats._registry[name] = this;
      return *this; 
    };
    template <typename... Args>
    StatWrapper& name(fmt::format_string<Args...> format_str, Args&&... args) { 
      return name(fmt::format(format_str, std::forward<Args>(args)...));
    };
    
    StatWrapper& desc(std::string desc) { _desc = desc; return *this; };

    void emit_to(YAML::Emitter& emitter) override {
      if        (std::holds_alternative<T*>(_ref)) {
        emitter << YAML::Key << _name;
        emitter << YAML::Value << *(std::get<T*>(_ref));
        if (!_desc.empty()) {
          emitter << YAML::Comment(_desc);
        }
      } else if (std::holds_alternative<std::vector<T>*>(_ref)) {
        emitter << YAML::Key << _name;
        if (!_desc.empty()) {
          emitter << YAML::Comment(_desc);
        }
        emitter << YAML::Value <<  YAML::BeginSeq;
        for (const auto _val : *(std::get<std::vector<T>*>(_ref))) {
          emitter << _val;
        }
        emitter << YAML::EndSeq;
      }

    };
};

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_STATS_H