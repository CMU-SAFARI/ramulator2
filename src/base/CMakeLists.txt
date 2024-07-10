add_library(ramulator2_base OBJECT)

target_include_directories(ramulator2_base PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_base
  PUBLIC
    base.h
    factory.h
    type.h
    exception.h
    logging.h
    debug.h
    param.h
    utils.h
    config.h
    clocked.h
    stats.h
    request.h
    serialization.h

  PRIVATE
    factory.cpp
    logging.cpp
    utils.cpp
    config.cpp
    stats.cpp
    request.cpp
)

target_link_libraries(ramulator2_base PRIVATE
  spdlog
  yaml-cpp
)
