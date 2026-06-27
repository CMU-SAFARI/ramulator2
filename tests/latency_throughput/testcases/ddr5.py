config = {
    "name": "DDR5",
    "dram_class": "DDR5",
    "org_preset": "DDR5_16Gb_x8",
    "timing_preset": "DDR5_4800AN",
    "controller_class": "GenericDDR",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 64,
    "nop_counters": (
        1, 33, 36, 38, 41, 44, 47, 50, 52, 55, 58, 65, 70, 80,
        90, 100, 125, 150, 200, 300, 500, 750, 1000, 2000,
        5000, 10000,
    ),
}
