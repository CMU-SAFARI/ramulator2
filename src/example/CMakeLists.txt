add_library(ramulator-example OBJECT)

target_sources(
  ramulator-example PRIVATE
  example_ifce.h
  impl/example_impl.cpp
  impl/example_serialization.cpp
  impl/complicated_impl.h   impl/complicated_impl.cpp
  impl/another_impl.cpp
  impl/yetanother_impl.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-example
)