"""Per-standard testcase data for smoke tests."""

from tests.smoke.testcases.ddr3 import CONFIG as DDR3_CONFIG
from tests.smoke.testcases.ddr4 import CONFIG as DDR4_CONFIG
from tests.smoke.testcases.ddr5 import CONFIG as DDR5_CONFIG
from tests.smoke.testcases.hbm import CONFIG as HBM1_CONFIG
from tests.smoke.testcases.hbm2 import CONFIG as HBM2_CONFIG
from tests.smoke.testcases.hbm3 import CONFIG as HBM3_CONFIG
from tests.smoke.testcases.lpddr5 import CONFIG as LPDDR5_CONFIG

STANDARDS = {
    "DDR3": DDR3_CONFIG,
    "DDR4": DDR4_CONFIG,
    "DDR5": DDR5_CONFIG,
    "HBM1": HBM1_CONFIG,
    "HBM2": HBM2_CONFIG,
    "HBM3": HBM3_CONFIG,
    "LPDDR5": LPDDR5_CONFIG,
}
