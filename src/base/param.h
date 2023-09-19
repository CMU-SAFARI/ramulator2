#ifndef     RAMULATOR_BASE_PARAM_H
#define     RAMULATOR_BASE_PARAM_H

#include <vector>
#include <unordered_map>
#include <string>
#include <optional>
#include <type_traits>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

#include "base/exception.h"


namespace Ramulator {

class Implementation;
class Params;
template<typename T> class _ParamChainer;

class _ParamGroupChainer {
  friend Params;
  private:
    YAML::Node _config;
    const std::string _impl_name;

    Params& _params;

    std::string _group_prefix = "";

    _ParamGroupChainer(YAML::Node& config, std::string impl_name, Params& params) : _config(config), _impl_name(impl_name), _params(params) {};
    _ParamGroupChainer& _set_group(std::string group_name) {
      if (_config[group_name]) {
        _config.reset(_config[group_name]);
        _group_prefix += (group_name + "::");
      } else {
        throw ConfigurationError("ParamGroup \"{}\" is not specified for implementation \"{}\".", group_name, _impl_name);
      }
      return *this;
    };

  public:
    template<typename T>
    _ParamChainer<T> param(std::string param_name);
    template<typename T>
    _ParamChainer<T> param(std::string_view param_name) { return param<T>(std::string(param_name)); };
    template<typename T>
    _ParamChainer<T> param(const char* param_name) { return param<T>(std::string(param_name)); };;

    _ParamGroupChainer& group(std::string group_name) {
      if (group_name == "impl" || group_name == "id") {
        throw ConfigurationError("In implementation \"{}\": ParamGroup name \"{}\" is reserved!", _impl_name, group_name);
      }

      _set_group(group_name);
      return *this;
    };
};


template<typename T>
class _ParamChainer {
  friend Params;
  friend _ParamGroupChainer;
  private:
    const YAML::Node& _config;
    const std::string _impl_name;

    Params& _params;

    std::string _name;
    std::string _name_prefix;
    std::string _desc;

    T _default_val;
    bool _default_val_set = false;
    bool _required = false;


  private:
    _ParamChainer(const YAML::Node& config, std::string impl_name, Params& params) : _config(config), _impl_name(impl_name), _params(params) {};
    _ParamChainer& _set_name(std::string name) { _name = name; return *this; };
    std::optional<T> _get() const { 
      if (_default_val_set && _required) {
        throw ConfigurationError("Param \"{}\" for implementation \"{}\" cannot both has a default value and be required.", _name_prefix + _name, _impl_name);
      }

      if (_config[_name]) {
        try {
          return _config[_name].as<T>();
        } catch (const YAML::BadConversion& e) {
          throw ConfigurationError("Failed to parse Param \"{}\" for implementation \"{}\".", _name_prefix + _name, _impl_name);
        }
      } else if (_required) {
        throw ConfigurationError("Param \"{}\" for implementation \"{}\" is required but not given.", _name_prefix + _name, _impl_name);
      } else if (_default_val_set) {
        return _default_val;
      } else {
        // throw ConfigurationError("No default value given for unspecified Param \"{}\" for implementation \"{}\" .", _name_prefix + _name, _impl_name);
        return std::nullopt;
      }
    };

  public:
    operator T() const { return *_get(); }

    _ParamChainer& desc(std::string desc);
    _ParamChainer& required() { _required = true; return *this; };
    _ParamChainer& default_val(T default_val) { _default_val = default_val; _default_val_set = true; return *this; };
    std::optional<T> optional() { return _get(); };
};


class Params {
  template <typename> friend class _ParamChainer;
  friend _ParamGroupChainer;
  friend Implementation;
  
  private:
    struct ParamInfo {
      std::string name;
      std::string desc;
    };

    Registry_t<ParamInfo> m_registry;

    std::string m_impl_name;
    YAML::Node m_config;

  public:
    Params() {};
    Params(const YAML::Node& config) : m_config(config) {};
    void set_impl_name(std::string impl_name) { m_impl_name = impl_name; };

  private:    
    template <typename T>
    _ParamChainer<T> _param(std::string param_name) {
      // Check for reserved key names
      if (param_name == "impl" || param_name == "id") {
        throw ConfigurationError("In implementation \"{}\": Param name \"{}\" is reserved!", m_impl_name, param_name);
      }

      _add_name(param_name);

      _ParamChainer<T> _p(m_config, m_impl_name, *this);
      _p._set_name(param_name);
      return _p;
    }

    _ParamGroupChainer _group(std::string group_name) {
      if (group_name == "impl" || group_name == "id") {
        throw ConfigurationError("In implementation \"{}\": ParamGroup name \"{}\" is reserved!", m_impl_name, group_name);
      }

      _ParamGroupChainer _pg(m_config, m_impl_name, *this);
      _pg._set_group(group_name);
      return _pg;
    }

    void _add_name(std::string param_name) {
      // Check for existing params
      if (auto it = m_registry.find(param_name); it != m_registry.end()) {
        throw InitializationError(
          "In implementation {}, param name \"{}\" is already registered.",
          m_impl_name,
          param_name
        );
      } else {
        m_registry[param_name] = {param_name};
      }
    }

    void _add_desc(std::string param_name, std::string param_desc) {
      // Check for existing params
      if (auto it = m_registry.find(param_name); it != m_registry.end()) {
        auto [name, info] = *it;
        info.desc = param_desc;
      } else {
        throw InitializationError(
          "In implementation {}, param name \"{}\" is not yet registered but a description is being added to it.",
          m_impl_name,
          param_name
        );
      }
    }
};

template<typename T>
_ParamChainer<T> _ParamGroupChainer::param(std::string param_name)  {
  std::string prefixed_param_name = _group_prefix + param_name;

  // Check for reserved key names
  if (param_name == "impl" || param_name == "id") {
    throw ConfigurationError("In implementation \"{}\": Param name \"{}\" is reserved!", _impl_name, prefixed_param_name);
  }

  _params._add_name(prefixed_param_name);

  _ParamChainer<T> _p(_config, _impl_name, _params);
  _p._set_name(param_name);
  _p._name_prefix = _group_prefix;
  return _p;
}

template<typename T>
_ParamChainer<T>& _ParamChainer<T>::desc(std::string desc) {
  _desc = desc;
  _params._add_desc(_name_prefix + _name, desc);
  return *this; 
};

}        // namespace Ramulator

#endif   // RAMULATOR_BASE_PARAM_H