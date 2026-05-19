add_library(ramulator-dram OBJECT)

target_sources(
  ramulator-dram PRIVATE
  node.h  node.cpp
  device.h  device.cpp
  dram_spec.h  dram_spec.cpp
  func_types.h

  commands/ACT.h  commands/ACT1.h  commands/ACT2.h
  commands/PREpb.h  commands/PREab.h
  commands/RD.h   commands/WR.h
  commands/RDA.h  commands/WRA.h
  commands/RD_S.h   commands/WR_S.h
  commands/RDA_S.h  commands/WRA_S.h
  commands/REFab.h  commands/REFpb.h
  commands/RFMab.h  commands/RFMpb.h
  commands/RCKSTRT.h  commands/RCKSTOP.h
  commands/CAS_RD.h  commands/CAS_WR.h
  commands/VRR.h
  commands/populate.h

  impl/DDR3.cpp
  impl/DDR4.cpp
  impl/DDR4_VRR.cpp
  impl/DDR5.cpp
  impl/DDR5_RFM.cpp
  impl/DDR5_RFM_VRR.cpp
  impl/DDR5_VRR.cpp
  impl/HBM1.cpp
  impl/HBM2.cpp
  impl/HBM3.cpp
  impl/HBM4.cpp
  impl/LPDDR5.cpp
  impl/LPDDR6.cpp
  impl/GDDR6.cpp
  impl/GDDR7.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-dram
)
