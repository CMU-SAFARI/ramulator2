add_library(ramulator-memorysystem OBJECT)

target_sources(
  ramulator-memorysystem PRIVATE
  bh_memory_system.h
  memory_system.h

  impl/bh_DRAM_system.cpp
  impl/dummy_memory_system.cpp
  impl/generic_DRAM_system.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-memorysystem
)