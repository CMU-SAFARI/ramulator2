import ramulator

CONFIG = dict(
    dram_class="HBM3",
    org_preset="HBM3_8Gb_8hi",
    timing_preset="HBM3_6400Mbps",
    dram_kwargs={},
    controller_class="HBM34",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=32,
)
