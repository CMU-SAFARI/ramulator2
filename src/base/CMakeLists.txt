add_library(ramulator-base OBJECT)

target_sources(
  ramulator-base PRIVATE
  base.h      
  factory.h   factory.cpp
  type.h
  exception.h
  logging.h   logging.cpp
  debug.h
  param.h 
  utils.h     utils.cpp
  config.h    config.cpp
  clocked.h
  stats.h     stats.cpp
  request.h   request.cpp
  serialization.h
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-base
)