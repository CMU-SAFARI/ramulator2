import ramulator


CONFIG = dict(
    dram_class="GDDR7",
    org_preset="GDDR7_16Gb_x8_4ch",
    timing_preset="GDDR7_TEST_28000",
    dram_kwargs={},
    controller_class="GDDR7",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=64,
)
