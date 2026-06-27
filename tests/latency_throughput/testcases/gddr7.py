config = {
    "name": "GDDR7",
    "dram_class": "GDDR7",
    "org_preset": "GDDR7_16Gb_x8",
    "timing_preset": "GDDR7_28000_PAM3",
    "controller_class": "GDDR7",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 16,
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 64,
    "nop_counters": (
        1, 6, 16, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 30, 32, 35,
        40, 45, 50, 60, 70, 80, 100, 135, 175, 250, 400, 1000,
        2000, 5000, 10000,
    ),
}
