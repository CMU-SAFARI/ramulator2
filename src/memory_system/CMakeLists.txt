add_library(ramulator-memorysystem OBJECT)
target_include_directories(ramulator-memorysystem PUBLIC ../../include)

target_sources(
  ramulator-memorysystem PRIVATE

  impl/dummy_memory_system.cpp
  impl/generic_DRAM_system.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-memorysystem
)