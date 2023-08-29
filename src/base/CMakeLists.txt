add_library(ramulator-base OBJECT)
target_include_directories(ramulator-base PUBLIC ../../include)

target_sources(
  ramulator-base PRIVATE
  factory.h   factory.cpp
  type.h
  exception.h
  logging.h   logging.cpp
  debug.h
  param.h 
  utils.h     utils.cpp
  config.cpp
  clocked.h
  stats.h     stats.cpp
  request.cpp
  serialization.h
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-base
)
