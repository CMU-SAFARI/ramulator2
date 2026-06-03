from tests.latency_throughput.testcases.gddr7 import config as _base

config = {
    **_base,
    "name": "GDDR7_start_with_rckstrt",
    "controller_kwargs": {
        **_base.get("controller_kwargs", {}),
        "rck_mode": "start_with_rckstrt",
    },
}