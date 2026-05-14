import ramulator

CONFIG = dict(
    dram_class="HBM4",
    org_preset="HBM4_32Gb_8Hi",
    timing_preset="HBM4_8000Mbps",
    dram_kwargs={},
    controller_class="HBM34",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=32,
)
