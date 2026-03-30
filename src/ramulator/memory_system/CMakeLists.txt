add_library(ramulator-memorysystem OBJECT)

target_sources(
  ramulator-memorysystem PRIVATE
  i_memory_system.h

  channel_mapper/i_channel_mapper.h
  channel_mapper/impl/cache_line_interleave.cpp
  channel_mapper/impl/passthrough_channel_mapper.cpp

  impl/generic_dram_system.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-memorysystem
)
