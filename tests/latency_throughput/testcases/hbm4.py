config = {
    "name": "HBM4",
    "dram_class": "HBM4",
    "org_preset": "HBM4_32Gb_8Hi",
    "timing_preset": "HBM4_8000Mbps",
    "controller_class": "HBM34",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 16,
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 32,
    "nop_counters": (
        1, 17, 18, 19, 20, 21, 22, 23, 24, 26, 28, 32, 36,
        40, 44, 48, 52, 56, 60, 70, 80, 100, 120, 160, 200,
        300, 400, 4000, 40000,
    ),
}
