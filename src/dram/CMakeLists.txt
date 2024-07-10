add_library(ramulator2_dram OBJECT)

target_include_directories(ramulator2_dram PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_dram
  PUBLIC
    dram.h
    node.h
    spec.h
    lambdas.h
    lambdas/preq.h
    lambdas/rowhit.h
    lambdas/rowopen.h
    lambdas/action.h
    lambdas/power.h

  PRIVATE
    impl/DDR3.cpp
    impl/DDR4.cpp
    impl/DDR4-VRR.cpp
    impl/DDR4-RVRR.cpp
    impl/DDR5.cpp
    impl/DDR5-VRR.cpp
    impl/DDR5-RVRR.cpp
    impl/LPDDR5.cpp
    impl/HBM.cpp
    impl/HBM2.cpp
    impl/HBM3.cpp
)

target_link_libraries(ramulator2_dram PRIVATE
  spdlog
  yaml-cpp
)
