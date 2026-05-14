add_library(ramulator-controller OBJECT)

target_sources(
  ramulator-controller PRIVATE
  i_controller.h
  controller_base.h
  controller_base.cpp

  scheduler/i_scheduler.h
  scheduler/impl/frfcfs.cpp

  refresh/i_refresh_manager.h
  refresh/impl/all_bank.cpp
  refresh/impl/per_bank.cpp
  refresh/impl/hbm34_per_bank_refresh.cpp
  refresh/impl/no_refresh.cpp

  rowpolicy/i_row_policy.h
  rowpolicy/impl/open.cpp
  rowpolicy/impl/closed_cap.cpp

  addr_mapper/i_addr_mapper.h
  addr_mapper/addr_mapper_base.h
  addr_mapper/addr_mapper_base.cpp
  addr_mapper/impl/ch_ra_ba_ro_co.cpp
  addr_mapper/impl/ro_ba_ra_co_ch.cpp
  addr_mapper/impl/mop4clxor.cpp
  addr_mapper/impl/passthrough_addr_mapper.cpp

  plugin/i_controller_plugin.h
  plugin/controller_validation_hook.h
  plugin/impl/command_counter.cpp
  plugin/impl/cmd_trace_recorder.cpp
  plugin/impl/issued_command_validation_hook.cpp

  impl/generic_ddr_controller.cpp
  impl/lpddr5_controller.cpp
  impl/hbm_controller_base.h
  impl/hbm_controller_base.cpp
  impl/hbm12_controller.cpp
  impl/hbm34_controller.cpp
)

target_link_libraries(
  ramulator
  PRIVATE
  ramulator-controller
)
