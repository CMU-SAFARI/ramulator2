import ramulator


CONFIG = dict(
    dram_class="DDR4",
    org_preset="DDR4_8Gb_x8",
    timing_preset="DDR4_2400R",
    dram_kwargs={},
    controller_class="GenericDDR",
    fast_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.NoRefresh(),
    ),
    full_ctrl_extra_kwargs=dict(
        refresh_manager=ramulator.refresh_manager.AllBank(scope="Rank"),
    ),
    full_streaming_requests=1_000_000,
    frontend_clock_ratio=4,
    stream_cols=8,
    nop_counters=[1, 9, 10, 11, 12, 13, 14, 15, 20, 30, 50, 100, 1000, 2000, 5000, 10000],
)
