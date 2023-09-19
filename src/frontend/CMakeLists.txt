add_library(ramulator-frontend OBJECT)

target_sources(
  ramulator-frontend PRIVATE
  frontend.h

  impl/memory_trace/loadstore_trace.cpp
  impl/memory_trace/readwrite_trace.cpp

  impl/processor/simpleO3/simpleO3.cpp
  impl/processor/simpleO3/core.h      impl/processor/simpleO3/core.cpp
  impl/processor/simpleO3/llc.h       impl/processor/simpleO3/llc.cpp
  impl/processor/simpleO3/trace.h     impl/processor/simpleO3/trace.cpp

  impl/external_wrapper/gem5_frontend.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-frontend
)