config = {
    "name": "HBM3",
    "dram_class": "HBM3",
    "org_preset": "HBM3_8Gb_8hi",
    "timing_preset": "HBM3_6400Mbps",
    "controller_class": "HBM34",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 16,
    "controller_kwargs": {"read_buffer_size": 128, "write_buffer_size": 64},
    "stream_cls": 32,
    "nop_counters": (
        1, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 32, 36,
        40, 44, 48, 52, 56, 60, 70, 80, 100, 120, 160, 200,
        300, 400, 4000, 40000,
    ),
}
