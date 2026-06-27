config = {
    "name": "LPDDR5",
    "dram_class": "LPDDR5",
    "org_preset": "LPDDR5_8Gb_x16",
    "timing_preset": "LPDDR5_6400",
    "controller_class": "LPDDR5",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 64,
    "nop_counters": (
        1, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 26, 30,
        40, 60, 100, 200, 1000, 2000, 5000, 10000,
    ),
}
