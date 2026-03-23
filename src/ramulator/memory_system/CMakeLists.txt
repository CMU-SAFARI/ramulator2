add_library(ramulator-memorysystem OBJECT)

target_sources(
  ramulator-memorysystem PRIVATE
  i_memory_system.h

  channel_mapper/i_channel_mapper.h
  channel_mapper/impl/channel_mappers.cpp

  impl/generic_dram_system.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-memorysystem
)
