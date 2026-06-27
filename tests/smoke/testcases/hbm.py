import ramulator

CONFIG = dict(
    dram_class="HBM1",
    org_preset="HBM1_2Gb",
    timing_preset="HBM1_2Gbps",
    dram_kwargs={},
    controller_class="HBM12",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=32,
)
