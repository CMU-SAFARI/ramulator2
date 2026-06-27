config = {
    "name": "GDDR6",
    "dram_class": "GDDR6",
    "org_preset": "GDDR6_8Gb_x16",
    "timing_preset": "GDDR6_14000_1250mV_double",
    "controller_class": "GenericDDR",
    "scheduler_class": "FRFCFSRowHit",
    "stagger_stream_rows": True,
    "frontend_clock_ratio": 16,
    "controller_kwargs": {"read_buffer_size": 32, "write_buffer_size": 32},
    "stream_cls": 64,
    "nop_counters": (
        1, 16, 17, 18, 19, 20, 21, 23, 25, 27, 30, 34,
        38, 44, 50, 60, 75, 100, 125, 200, 400, 1000,
        10000,
    ),
}
