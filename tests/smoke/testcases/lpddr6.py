import ramulator

CONFIG = dict(
    dram_class="LPDDR6",
    org_preset="LPDDR6_16Gb_x12",
    timing_preset="LPDDR6_10667_BL24",
    dram_kwargs={},
    controller_class="LPDDR6",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=8,
)
