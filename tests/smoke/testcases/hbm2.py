import ramulator

CONFIG = dict(
    dram_class="HBM2",
    org_preset="HBM2_2Gb",
    timing_preset="HBM2_2000Mbps",
    dram_kwargs={},
    controller_class="HBM",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=32,
)
