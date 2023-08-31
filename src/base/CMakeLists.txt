add_library(ramulator-base OBJECT)
target_include_directories(ramulator-base PUBLIC ../../include)

target_sources(
  ramulator-base PRIVATE
        ../../include/ramulator/base/factory.h factory.cpp
        ../../include/ramulator/base/type.h
        ../../include/ramulator/base/exception.h
        ../../include/ramulator/base/logging.h logging.cpp
        ../../include/ramulator/base/debug.h
        ../../include/ramulator/base/param.h
        ../../include/ramulator/base/utils.h utils.cpp
  config.cpp
        ../../include/ramulator/base/clocked.h
        ../../include/ramulator/base/stats.h stats.cpp
  request.cpp
        ../../include/ramulator/base/serialization.h
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-base
)
