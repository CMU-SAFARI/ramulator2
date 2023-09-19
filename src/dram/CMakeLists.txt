add_library(ramulator-dram OBJECT)

target_sources(
  ramulator-dram PRIVATE
  dram.h  node.h  spec.h  lambdas.h  
  
  lambdas/preq.h  lambdas/rowhit.h  lambdas/rowopen.h 

  impl/DDR3.cpp
  impl/DDR4.cpp
  impl/DDR4-VRR.cpp
  impl/DDR5.cpp
  impl/LPDDR5.cpp
  impl/HBM.cpp
  impl/HBM2.cpp
  impl/HBM3.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-dram
)
