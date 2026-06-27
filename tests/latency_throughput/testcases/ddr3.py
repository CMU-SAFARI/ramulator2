config = {
    "name": "DDR3",
    "dram_class": "DDR3",
    "org_preset": "DDR3_2Gb_x8",
    "timing_preset": "DDR3_1600H",
    "controller_class": "GenericDDR",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 128,
    "nop_counters": (
        1, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 30, 32, 35,
        38, 40, 45, 50, 55, 65, 75, 85, 100, 125, 175, 300,
        1000, 2000, 10000,
    ),
}
