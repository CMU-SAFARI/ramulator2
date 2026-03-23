"""Per-standard testcase data for latency-throughput tests.

Single source of truth for all DRAM standard test configurations.
"""

from tests.latency_throughput.testcases.ddr3 import CONFIG as DDR3_CONFIG
from tests.latency_throughput.testcases.ddr4 import CONFIG as DDR4_CONFIG
from tests.latency_throughput.testcases.ddr5 import CONFIG as DDR5_CONFIG
from tests.latency_throughput.testcases.hbm import CONFIG as HBM1_CONFIG
from tests.latency_throughput.testcases.hbm2 import CONFIG as HBM2_CONFIG
from tests.latency_throughput.testcases.hbm3 import CONFIG as HBM3_CONFIG
from tests.latency_throughput.testcases.lpddr5 import CONFIG as LPDDR5_CONFIG

STANDARDS = {
    "DDR3": DDR3_CONFIG,
    "DDR4": DDR4_CONFIG,
    "DDR5": DDR5_CONFIG,
    "HBM1": HBM1_CONFIG,
    "HBM2": HBM2_CONFIG,
    "HBM3": HBM3_CONFIG,
    "LPDDR5": LPDDR5_CONFIG,
}
