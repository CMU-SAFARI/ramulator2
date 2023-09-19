#ifndef     RAMULATOR_BASE_DEBUG_H
#define     RAMULATOR_BASE_DEBUG_H

#include <vector>
#include <string>
#include <type_traits>

#include "base/logging.h"

namespace Ramulator {

namespace Debug {

/**
 * @brief       Type trait for debug flags
 * 
 */
template <class T>
inline constexpr bool is_debug_enabled = std::false_type::value;


#define DECLARE_DEBUG_FLAG(flagT) \
  namespace Debug { \
    struct flagT; \
  }

#ifdef RAMULATOR_DEBUG
#define ENABLE_DEBUG_FLAG(flagT) \
  template <> \
  inline constexpr bool Debug::is_debug_enabled<Debug::flagT> = std::true_type::value;

#define DEBUG_LOG(flagT, logger, msg, ...) \
  if constexpr (Debug::is_debug_enabled<Debug::flagT>) { \
    logger->debug(msg, __VA_ARGS__); \
  }
#else
#define ENABLE_DEBUG_FLAG(flagT)
#define DEBUG_LOG(flagT, logger, msg, ...)
#endif
}        // namespace Debug

}        // namespace Ramulator


#endif   // RAMULATOR_BASE_DEBUG_H