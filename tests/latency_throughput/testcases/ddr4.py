config = {
    "name": "DDR4",
    "dram_class": "DDR4",
    "org_preset": "DDR4_8Gb_x8",
    "timing_preset": "DDR4_2400R",
    "controller_class": "GenericDDR",
    "refresh_scope": "Rank",
    "stream_cls": 128,
    "nop_counters": (1, *range(9, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
