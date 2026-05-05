config = {
    "name": "LPDDR5",
    "dram_class": "LPDDR5",
    "org_preset": "LPDDR5_8Gb_x16",
    "timing_preset": "LPDDR5_6400",
    "controller_class": "LPDDR5",
    "refresh_scope": "Rank",
    "stream_cls": 64,
    "nop_counters": (1, *range(5, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
