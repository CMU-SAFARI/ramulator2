config = {
    "name": "DDR4",
    "dram_class": "DDR4",
    "org_preset": "DDR4_8Gb_x8",
    "timing_preset": "DDR4_2400R",
    "controller_class": "GenericDDR",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 128,
    "nop_counters": (
        1, 16, 18, 19, 20, 21, 22, 23, 24, 26, 28, 30, 32, 35, 37,
        40, 45, 50, 60, 75, 100, 125, 175, 300, 1000, 2000,
        5000, 10000,
    ),
}
