#ifndef RAMULATOR_BASE_LOGGER_H
#define RAMULATOR_BASE_LOGGER_H

#include <memory>
#include <string>

namespace Ramulator {

class Logger {
 public:
  Logger();
  explicit Logger(const std::string& name, const std::string& pattern = "");
  ~Logger();

  Logger(Logger&&) noexcept;
  Logger& operator=(Logger&&) noexcept;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void debug(const std::string& msg);
  void info(const std::string& msg);
  void warn(const std::string& msg);
  void error(const std::string& msg);

  bool should_log_debug() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace Ramulator

#endif  // RAMULATOR_BASE_LOGGER_H
