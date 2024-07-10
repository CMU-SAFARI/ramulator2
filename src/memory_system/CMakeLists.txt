add_library(ramulator2_memory_system OBJECT)

target_include_directories(ramulator2_memory_system PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_memory_system
  PUBLIC
    bh_memory_system.h
    memory_system.h

  PRIVATE
    impl/bh_DRAM_system.cpp
    impl/dummy_memory_system.cpp
    impl/generic_DRAM_system.cpp
)

target_link_libraries(ramulator2_memory_system PRIVATE
  spdlog
  yaml-cpp
)