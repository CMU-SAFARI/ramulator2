import ramulator

CONFIG = dict(
    dram_class="DDR3",
    org_preset="DDR3_2Gb_x8",
    timing_preset="DDR3_1600H",
    dram_kwargs={},
    controller_class="GenericDDR",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=8,
)
