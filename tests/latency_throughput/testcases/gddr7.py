config = {
    "name": "GDDR7",
    "dram_class": "GDDR7",
    "org_preset": "GDDR7_16Gb_x8_4ch",
    "timing_preset": "GDDR7_TEST_28000_PAM3",
    "controller_class": "GDDR7",
    "controller_kwargs": {"read_buffer_size": 192, "write_buffer_size": 192},
    "stream_cls": 64,
    "nop_counters": (1, *range(3, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
