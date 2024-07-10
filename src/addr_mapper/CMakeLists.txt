add_library(ramulator2_addr_mapper OBJECT)

target_include_directories(ramulator2_addr_mapper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_addr_mapper
  PUBLIC
    addr_mapper.h
    impl/rit.h

  PRIVATE
    impl/linear_mappers.cpp
    impl/rit.cpp
)

target_link_libraries(ramulator2_addr_mapper PRIVATE
  spdlog
  yaml-cpp
)