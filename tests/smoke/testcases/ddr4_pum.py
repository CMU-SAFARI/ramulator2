import ramulator

# 64 subarrays per bank.
CONFIG = dict(
    dram_class="DDR4_PUM",
    org_preset="DDR4_8Gb_x8",
    timing_preset="DDR4_2400R",
    dram_kwargs=dict(rows_per_subarray=1024),
    controller_class="GenericDDR",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    frontend_clock_ratio=4,
    stream_cls=8,
)
