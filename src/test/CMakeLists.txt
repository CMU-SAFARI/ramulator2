add_library(ramulator2_test OBJECT)

target_include_directories(ramulator2_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_test
  PUBLIC
    test_ifce.h

  PRIVATE
    test_impl.cpp
)

target_link_libraries(ramulator2_test PRIVATE
  spdlog
  yaml-cpp
)