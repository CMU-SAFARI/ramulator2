add_library(ramulator-power OBJECT)

target_sources(
  ramulator-power PRIVATE
  i_power_model.h

  hbm/state.h
  hbm/energy.h
  hbm/memspec.h
  hbm/data_pattern_model.h
  hbm/rate_scaling.h
  hbm/calculation.h
  hbm/calculation.cpp

  ayna.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-power
)
