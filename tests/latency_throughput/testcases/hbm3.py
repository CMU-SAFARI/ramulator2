config = {
    "name": "HBM3",
    "dram_class": "HBM3",
    "org_preset": "HBM3_8Gb",
    "timing_preset": "HBM3_4800Mbps",
    "controller_class": "HBM",
    "refresh_scope": "Channel",
    "stream_cls": 4,
    "nop_counters": (1, *range(5, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
