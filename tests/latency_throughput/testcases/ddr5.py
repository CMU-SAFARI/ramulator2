config = {
    "name": "DDR5",
    "dram_class": "DDR5",
    "org_preset": "DDR5_16Gb_x8",
    "timing_preset": "DDR5_4800AN",
    "controller_class": "GenericDDR",
    "refresh_scope": "Rank",
    "stream_cls": 64,
    "nop_counters": (1, *range(15, 21), 25, 30, 40, 50, 75, 100, 1000, 2000, 5000, 10000),
}
