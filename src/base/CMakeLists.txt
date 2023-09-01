add_library(ramulator-base OBJECT)
target_include_directories(ramulator-base PUBLIC ../../include)

target_sources(
  ramulator-base PRIVATE
        factory.cpp
        logging.cpp
        utils.cpp
        config.cpp
        stats.cpp
        request.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-base
)
