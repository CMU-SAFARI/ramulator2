add_library(ramulator-memorysystem OBJECT)

target_sources(
  ramulator-memorysystem PRIVATE
  memory_system.h

  impl/dummy_memory_system.cpp
  impl/generic_DRAM_system.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-memorysystem
)