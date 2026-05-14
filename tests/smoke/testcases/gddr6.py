import ramulator

CONFIG = dict(
    dram_class="GDDR6",
    org_preset="GDDR6_8Gb_x16",
    timing_preset="GDDR6_2000_1250mV_double",
    dram_kwargs={},
    controller_class="GenericDDR",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=64,
)
