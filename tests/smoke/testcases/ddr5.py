import ramulator

CONFIG = dict(
    dram_class="DDR5",
    org_preset="DDR5_16Gb_x8",
    timing_preset="DDR5_4800AN",
    dram_kwargs={},
    controller_class="GenericDDR",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=8,
)
