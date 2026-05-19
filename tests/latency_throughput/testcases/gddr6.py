config = {
    "name": "GDDR6",
    "dram_class": "GDDR6",
    "org_preset": "GDDR6_8Gb_x16",
    "timing_preset": "GDDR6_2000_1250mV_double",
    "controller_class": "GenericDDR",
    "controller_kwargs": {"read_buffer_size": 64, "write_buffer_size": 64},
    "stream_cls": 64,
    "nop_counters": (1, *range(3, 16), 20, 30, 50, 100, 1000, 2000, 5000, 10000),
}
