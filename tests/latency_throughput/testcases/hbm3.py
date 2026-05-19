config = {
    "name": "HBM3",
    "dram_class": "HBM3",
    "org_preset": "HBM3_8Gb_8hi",
    "timing_preset": "HBM3_6400Mbps",
    "controller_class": "HBM34",
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 32,
    "nop_counters": (1, *range(5, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
