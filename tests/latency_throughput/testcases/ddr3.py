config = {
    "name": "DDR3",
    "dram_class": "DDR3",
    "org_preset": "DDR3_2Gb_x8",
    "timing_preset": "DDR3_1600H",
    "controller_class": "GenericDDR",
    "stream_cls": 128,
    "nop_counters": (1, *range(9, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
