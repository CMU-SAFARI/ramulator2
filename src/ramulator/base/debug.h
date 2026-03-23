#ifndef RAMULATOR_BASE_DEBUG_H
#define RAMULATOR_BASE_DEBUG_H

#ifdef RAMULATOR_DEBUG
#include <fmt/format.h>
#define DEBUG_LOG(logger, ...)             \
  do {                                     \
    auto&& _dl = (logger);                 \
    if (_dl.should_log_debug())            \
      _dl.debug(fmt::format(__VA_ARGS__)); \
  } while (0)
#else
#define DEBUG_LOG(logger, ...)
#endif

#endif  // RAMULATOR_BASE_DEBUG_H
