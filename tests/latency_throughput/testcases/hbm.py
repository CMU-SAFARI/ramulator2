config = {
    "name": "HBM1",
    "dram_class": "HBM1",
    "org_preset": "HBM1_2Gb",
    "timing_preset": "HBM1_2Gbps",
    "controller_class": "HBM12",
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 64,
    "nop_counters": (1, *range(3, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
