#include "ramulator/base/logger.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Ramulator {

struct LoggerData {
  std::string name;
  bool debug_enabled = true;
};

struct Logger::Impl {
  LoggerData data;
};

// Idempotent logger registry
static std::unordered_map<std::string, LoggerData>& logger_registry() {
  static std::unordered_map<std::string, LoggerData> registry;
  return registry;
}

Logger::Logger() : m_impl(std::make_unique<Impl>()) {
}

Logger::Logger(const std::string& name, const std::string&) : m_impl(std::make_unique<Impl>()) {
  std::string full_name = "Ramulator::" + name;

  // Return existing logger if already created (idempotent for library reloads)
  auto& registry = logger_registry();
  if (auto it = registry.find(full_name); it != registry.end()) {
    m_impl->data = it->second;
    return;
  }

  m_impl->data.name = full_name;
  m_impl->data.debug_enabled = true;
  registry[full_name] = m_impl->data;
}

Logger::~Logger() = default;
Logger::Logger(Logger&&) noexcept = default;
Logger& Logger::operator=(Logger&&) noexcept = default;

void Logger::debug(const std::string& msg) {
  std::cerr << "[" << m_impl->data.name << "] [\033[36mdebug\033[0m] " << msg << "\n";
}

void Logger::info(const std::string& msg) {
  std::cerr << "[" << m_impl->data.name << "] [\033[32minfo\033[0m] " << msg << "\n";
}

void Logger::warn(const std::string& msg) {
  std::cerr << "[" << m_impl->data.name << "] [\033[33mwarn\033[0m] " << msg << "\n";
}

void Logger::error(const std::string& msg) {
  std::cerr << "[" << m_impl->data.name << "] [\033[31merror\033[0m] " << msg << "\n";
}

bool Logger::should_log_debug() const {
  return m_impl->data.debug_enabled;
}

}  // namespace Ramulator
