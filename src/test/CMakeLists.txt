add_library(ramulator-test OBJECT)
target_include_directories(ramulator-test PUBLIC ../../include)

target_sources(
  ramulator-test PRIVATE
  test_ifce.h
  test_impl.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-test
)