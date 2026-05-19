add_library(ramulator-frontend OBJECT)

target_sources(
  ramulator-frontend PRIVATE
  i_frontend.h

  impl/external.cpp
  impl/memory_trace/loadstore_trace.cpp
  impl/memory_trace/readwrite_trace.cpp
  impl/memory_trace/latency_throughput_trace.cpp

  impl/processor/simpleO3/simpleO3.cpp
  impl/processor/simpleO3/core.h      impl/processor/simpleO3/core.cpp
  impl/processor/simpleO3/llc.h       impl/processor/simpleO3/llc.cpp

  impl/processor/bhO3/bhO3.cpp
  impl/processor/bhO3/bhcore.h        impl/processor/bhO3/bhcore.cpp
  impl/processor/bhO3/bhllc.h         impl/processor/bhO3/bhllc.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-frontend
)
