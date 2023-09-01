add_library(ramulator-dram OBJECT)
target_include_directories(ramulator-dram PUBLIC ../../include)

target_sources(
  ramulator-dram PRIVATE
  dram.h  node.h  spec.h  lambdas.h  
  
  lambdas/preq.h  lambdas/rowhit.h  lambdas/rowopen.h 

  impl/DDR3.cpp
  impl/DDR4.cpp
  impl/DDR4-VRR.cpp
  impl/DDR5.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-dram
)
