add_library(ramulator2_example OBJECT)

target_include_directories(ramulator2_example PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_example
  PUBLIC
    example_ifce.h
    impl/complicated_impl.h

  PRIVATE
    impl/example_impl.cpp
    impl/example_serialization.cpp
    impl/complicated_impl.cpp
    impl/another_impl.cpp
    impl/yetanother_impl.cpp
)

target_link_libraries(ramulator2_example PRIVATE
  spdlog
  yaml-cpp
)