config = {
    "name": "DDR4_PUM",
    "dram_class": "DDR4_PUM",
    "org_preset": "DDR4_8Gb_x8",
    "timing_preset": "DDR4_2400R",
    "controller_class": "GenericDDR",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 8,
    "stream_cls": 128,
    "nop_counters": (
        1, 16, 18, 20, 22, 24, 28, 32, 37, 45, 60, 100, 175, 300,
        1000, 2000, 5000, 10000,
    ),
}
