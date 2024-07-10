add_library(ramulator2_controller OBJECT)

target_include_directories(ramulator2_controller PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_sources(ramulator2_controller
  PUBLIC
    bh_controller.h
    bh_scheduler.h
    controller.h
    scheduler.h
    plugin.h
    refresh.h
    rowpolicy.h
    impl/plugin/blockhammer/blockhammer_throttler.h
    impl/plugin/blockhammer/blockhammer_util.h
    impl/plugin/blockhammer/blockhammer.h
    impl/plugin/device_config/device_config.h
    impl/plugin/bliss/bliss.h
    impl/plugin/prac/prac.h

  PRIVATE
    impl/bh_dram_controller.cpp
    impl/dummy_controller.cpp
    impl/generic_dram_controller.cpp
    impl/prac_dram_controller.cpp
    impl/scheduler/bh_scheduler.cpp
    impl/scheduler/blocking_scheduler.cpp
    impl/scheduler/generic_scheduler.cpp
    impl/scheduler/bliss_scheduler.cpp
    impl/scheduler/prac_scheduler.cpp
    impl/refresh/all_bank_refresh.cpp
    impl/rowpolicy/basic_rowpolicies.cpp
    impl/plugin/trace_recorder.cpp
    impl/plugin/cmd_counter.cpp
    impl/plugin/para.cpp
    impl/plugin/graphene.cpp
    impl/plugin/oracle_rh.cpp
    impl/plugin/twice.cpp
    impl/plugin/hydra.cpp
    impl/plugin/rrs.cpp
    impl/plugin/aqua.cpp
    impl/plugin/rfm_manager.cpp
    impl/plugin/blockhammer/blockhammer.cpp
    impl/plugin/device_config/device_config.cpp
    impl/plugin/bliss/bliss.cpp
    impl/plugin/prac/prac.cpp
)

target_link_libraries(ramulator2_controller PRIVATE
  spdlog
  yaml-cpp
)