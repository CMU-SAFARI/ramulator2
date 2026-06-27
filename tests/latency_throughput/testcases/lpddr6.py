config = {
    "name": "LPDDR6",
    "dram_class": "LPDDR6",
    "org_preset": "LPDDR6_16Gb_x12",
    "timing_preset": "LPDDR6_10667_BL24",
    "controller_class": "LPDDR6",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 64,
    "nop_counters": (
        1, 24, 26, 28, 30, 32, 35, 40, 45, 50, 55, 60, 70,
        80, 100, 120, 150, 200, 300, 500, 1000, 10000,
    ),
}
