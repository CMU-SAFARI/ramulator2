config = {
    "name": "HBM2",
    "dram_class": "HBM2",
    "org_preset": "HBM2_2Gb",
    "timing_preset": "HBM2_2000Mbps",
    "controller_class": "HBM12",
    "stream_cls": 8,
    "nop_counters": (1, *range(3, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
