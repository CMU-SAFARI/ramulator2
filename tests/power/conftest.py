import pytest

from tests.power.ayna_reference import find_ayna_bin


@pytest.fixture(scope="session")
def ayna_bin():
    """Directory with Ayna's HBM2_runner / HBM3_runner; skips when they are not available."""
    path = find_ayna_bin()
    if path is None:
        pytest.skip("Ayna's runners not found: set HBM_POWER_AYNA_BIN or HBM_POWER_AYNA")
    return path
