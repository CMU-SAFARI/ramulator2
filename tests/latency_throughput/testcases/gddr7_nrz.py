from tests.latency_throughput.testcases.gddr7 import config as _base

config = {
    **_base,
    "name": "GDDR7_NRZ",
    "dram_kwargs": {
        **_base.get("dram_kwargs", {}),
        "encoding": "NRZ",
    },
}