add_library(ramulator-base OBJECT)

target_sources(
  ramulator-base PRIVATE
  base.h      
  factory.h   factory.cpp
  type.h
logger.h    logger.cpp
  debug.h
  param.h 
  utils.h     utils.cpp
  config_node.h
  config.h    config.cpp
  stats.h
  request.h   request.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-base
)