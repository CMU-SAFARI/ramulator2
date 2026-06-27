config = {
    "name": "HBM2",
    "dram_class": "HBM2",
    "org_preset": "HBM2_2Gb",
    "timing_preset": "HBM2_2000Mbps",
    "controller_class": "HBM12",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 16,
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 32,
    "nop_counters": (
        1, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        22, 24, 28, 32, 36, 40, 48, 60, 80, 120, 200, 400,
        4000, 40000,
    ),
}
