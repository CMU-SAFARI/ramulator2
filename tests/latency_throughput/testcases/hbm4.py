config = {
    "name": "HBM4",
    "dram_class": "HBM4",
    "org_preset": "HBM4_32Gb_8Hi",
    "timing_preset": "HBM4_8000Mbps",
    "controller_class": "HBM34",
    "stream_cls": 32,
    "nop_counters": (1, *range(5, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
