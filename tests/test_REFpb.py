import ramulator

from ramulator._ramulator_test import _DeviceUnderTest
import ramulator.dram.lpddr5 as lpddr5_mod
print("using lpddr5.py:", lpddr5_mod.__file__)


'''
For testing run: 
cmake -S . -B build -DRAMULATOR_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
PYTHONPATH="$PWD/python:$PWD/tests" pytest -v ./tests/test_REFpb.py
'''

def make_lpddr5_device():
    lpddr5 = ramulator.dram.LPDDR5(
        org_preset="LPDDR5_8Gb_x16",
        timing_preset="LPDDR5_6400",
        rank=1,
    )
    return _DeviceUnderTest(lpddr5.to_config())


def get_addr(row: int = 0, column: int = 0, bankgroup: int = 0, bank: int = 0):
    #oder: channel, rank, bankgroup, bank, row, column
    return [0, 0, bankgroup, bank, row, column]

def assert_DUT(dut, command, addr, clk, preq=None, timing_OK = None, ready=None, row_open=None, row_hit=None):
    p = dut.probe(command, addr, clk)

    if preq is not None: 
        # check if the prerequisite command is correct for given cycle and command
        assert p["preq"] == preq, (command, clk, p)

    #do the same for timing, ready and row info
    if timing_OK is not None:
        assert p["timing_OK"] is timing_OK, (command, clk, p)
    if ready is not None:
        assert p["ready"] is ready, (command, clk, p)
    if row_open is not None:
        assert p["row_open"] is row_open, (command, clk, p)
    if row_hit is not None:
        assert p["row_hit"] is row_hit, (command, clk, p)
    return p
    

def test_REFpb_ACT_same_bank():
    dut = make_lpddr5_device()

    #a0 and a1 in the same bank
    a0 = get_addr(row=123, bankgroup=0, bank=0)
    a1 = get_addr(row=456, bankgroup=0, bank=0)

    nRAS = dut.timing("nRAS")
    nRP = dut.timing("nRP")
    nRFCpb = dut.timing("nRFCpb")

    dut.issue("ACT1", a0, 0)
    dut.issue("ACT2", a0, 1)
    dut.issue("PREpb", a0, nRAS)

    ref_clk = nRAS + nRP
    dut.issue("REFpb", a0, ref_clk)

    # REFpb<->ACT (same bank) = tRFCpb
    if nRFCpb > 1:
        assert_DUT(dut, "ACT1", a1, ref_clk +nRFCpb-1, preq="ACT1", timing_OK=False, ready=False)
    
    assert_DUT(dut, "ACT1", a1, ref_clk +nRFCpb, preq="ACT1", timing_OK=True, ready=True)


def test_REFpb_needs_PRE_pb_when_bank_open():
    dut = make_lpddr5_device()
    a0 = get_addr(row=123, bankgroup=0, bank=0)
    dut.issue("ACT1", a0, 0)
    dut.issue("ACT2", a0, 1)
    assert_DUT(dut, "REFpb", a0, 3, preq="PREpb", ready=False)

def test_ACT_RD_REFpb():
    dut = make_lpddr5_device()
    a0 = get_addr(row=123, bankgroup=0, bank=0)
    dut.issue("ACT1", a0, 0)
    dut.issue("ACT2", a0, 1)


def test_refpb_waits_for_nrc_after_act1_then_requires_precharge():
    dut = make_lpddr5_device()
    a0 = get_addr(row=123, bankgroup=0, bank=0)

    nRC = dut.timing("nRC")
    nRAS = dut.timing("nRAS")
    nRP = dut.timing("nRP")

    dut.issue("ACT1", a0, 0)
    dut.issue("ACT2", a0, 1)


    # ACT1<->REFpb = nRC (?)
    if nRC > 1:
        assert_DUT(dut, "REFpb", a0, nRC - 1, preq="PREpb", timing_OK=False, ready=False)

    # cannot issue REFpb directly at nRC because the bank is open
    # timing may be OK, but the prerequisite should still be PREpb
    assert_DUT( dut, "REFpb", a0, nRC, preq="PREpb", timing_OK=True, ready=False)

    # ACT1<->PREpb = nRAS
    dut.issue("PREpb", a0, nRAS)

    assert_DUT(dut, "RD", a0, nRAS + 1, preq="ACT1", row_open=False, row_hit=False)

    # PREpb<->REFpb = nRP
    if nRP > 1:
        assert_DUT(dut, "REFpb", a0, nRAS + nRP - 1, preq="REFpb", timing_OK=False, ready=False)

    assert_DUT(dut, "REFpb", a0, nRAS + nRP, preq="REFpb", timing_OK=True, ready=True)

def test_prepb_only_closes_target_bank():
    dut = make_lpddr5_device()
    a_bank0 = get_addr(row=111, bankgroup=0, bank=0)
    a_bank1 = get_addr(row=222, bankgroup=0, bank=1)

    nRAS = dut.timing("nRAS")
    dut.issue("ACT1", a_bank0, 0)
    dut.issue("ACT2", a_bank0, 1)
    open_bank1_clk = nRAS
    dut.issue("ACT1", a_bank1, open_bank1_clk)
    dut.issue("ACT2", a_bank1, open_bank1_clk + 1)

    #precharge bank 0 only
    pre_clk = open_bank1_clk + nRAS
    dut.issue("PREpb", a_bank0, pre_clk)
    assert_DUT(dut, "RD", a_bank0, pre_clk + 1, preq="ACT1", row_open=False, row_hit=False)
    assert_DUT(dut, "RD", a_bank1, pre_clk + 1, preq="RD", row_open=True, row_hit=True)


def test_refpb_to_act_same_bank_respects_nrfcpb():
    dut = make_lpddr5_device()

    a_refreshed_bank = get_addr(row=123, bankgroup=0, bank=0)
    a_same_bank_new_row = get_addr(row=456, bankgroup=0, bank=0)

    nRFCpb = dut.timing("nRFCpb")

    ref_clk = 0

    # REFpb to closed bank
    assert_DUT(dut, "REFpb", a_refreshed_bank, ref_clk, preq="REFpb", timing_OK=True, ready=True)

    dut.issue("REFpb", a_refreshed_bank, ref_clk)

    # REFpb <-> ACT1 = nRFCpb
    assert_DUT(dut, "ACT1", a_same_bank_new_row, ref_clk + nRFCpb - 1, preq="ACT1",  timing_OK=False, ready=False)
    assert_DUT(dut, "ACT1", a_same_bank_new_row, ref_clk + nRFCpb, preq="ACT1", timing_OK=True, ready=True)

def test_refab_to_act_any_bank_respects_nrfc():
    dut = make_lpddr5_device()

    a_bank0 = get_addr(row=123, bankgroup=0, bank=0)
    a_bank1 = get_addr(row=456, bankgroup=0, bank=1)

    nRFC = dut.timing("nRFC")

    ref_clk = 0

    # REFab to closed rank / all banks
    refab_addr = [0, 0, -1, -1, -1, -1]

    assert_DUT(dut, "REFab", refab_addr, ref_clk, preq="REFab", timing_OK=True, ready=True)

    dut.issue("REFab", refab_addr, ref_clk)

    assert_DUT(dut, "ACT1", a_bank0, ref_clk + nRFC - 1, preq="ACT1", timing_OK=False, ready=False)
    assert_DUT(dut, "ACT1", a_bank1, ref_clk + nRFC - 1, preq="ACT1", timing_OK=False, ready=False)

    assert_DUT(dut, "ACT1", a_bank0, ref_clk + nRFC, preq="ACT1", timing_OK=True, ready=True)
    assert_DUT(dut, "ACT1", a_bank1, ref_clk + nRFC, preq="ACT1", timing_OK=True, ready=True)


def test_refpb_blocks_same_bank_for_nrfcpb_and_other_bank_for_npbr2act():
    dut = make_lpddr5_device()
    a_refreshed_bank = get_addr(row=123, bankgroup=0, bank=0)
    a_same_bank = get_addr(row=456, bankgroup=0, bank=0)
    a_other_bank = get_addr(row=789, bankgroup=0, bank=1)
    nRFCpb = dut.timing("nRFCpb")
    nPBR2ACT = dut.timing("nPBR2ACT")

    ref_clk = 0
    dut.issue("REFpb", a_refreshed_bank, ref_clk)

    # same refreshed bank is blocked for full tRFCpb
    assert_DUT(dut, "ACT1", a_same_bank, ref_clk + nRFCpb - 1, preq="ACT1", timing_OK=False, ready=False)
    assert_DUT(dut, "ACT1", a_same_bank, ref_clk + nRFCpb, preq="ACT1", timing_OK=True, ready=True)

    # different bank is blocked only for tpbr2act
    assert_DUT(dut, "ACT1", a_other_bank, ref_clk + nPBR2ACT - 1, preq="ACT1", timing_OK=False, ready=False)
    assert_DUT(dut, "ACT1", a_other_bank, ref_clk + nPBR2ACT, preq="ACT1", timing_OK=True, ready=True)


def test_refpb_to_refab_respects_nrfcpb():
    dut = make_lpddr5_device()
    a_bank0 = get_addr(row=123, bankgroup=0, bank=0)
    refab_addr = [0, 0, -1, -1, -1, -1]

    nRFCpb = dut.timing("nRFCpb")
    refpb_clk = 0
    dut.issue("REFpb", a_bank0, refpb_clk)

    # REFpb <-> REFab = nRFCpb
    assert_DUT(dut, "REFab", refab_addr, refpb_clk + nRFCpb - 1, preq="REFab", timing_OK=False, ready=False)
    assert_DUT( dut, "REFab", refab_addr, refpb_clk + nRFCpb, preq="REFab", timing_OK=True, ready=True)

def test_refab_to_refpb_respects_nrfcab():
    dut = make_lpddr5_device()
    refab_addr = [0, 0, -1, -1, -1, -1]
    a_bank0 = get_addr(row=123, bankgroup=0, bank=0)
    nRFCab = dut.timing("nRFC")  # Ramulator name for JEDEC tRFCab, is this on purpose??

    refab_clk = 0
    dut.issue("REFab", refab_addr, refab_clk)
    # what we see is: timing_OK=True, ready=True
    assert_DUT(dut, "REFpb", a_bank0, refab_clk + nRFCab - 1, preq="REFpb", timing_OK=False, ready=False)
    assert_DUT(dut, "REFpb", a_bank0, refab_clk + nRFCab, preq="REFpb", timing_OK=True, ready=True)

def test_refpb_to_refab_respects_nrfcpb():
    dut = make_lpddr5_device()
    a_bank0 = get_addr(row=123, bankgroup=0, bank=0)
    refab_addr = [0, 0, -1, -1, -1, -1]

    nRFCpb = dut.timing("nRFCpb")
    refpb_clk = 0
    dut.issue("REFpb", a_bank0, refpb_clk)

    # REFpb -> REFab must wait tRFCpb / nRFCpb.
    assert_DUT(dut, "REFab", refab_addr, refpb_clk + nRFCpb - 1, preq="REFab", timing_OK=False, ready=False)
    assert_DUT(dut,"REFab", refab_addr, refpb_clk + nRFCpb, preq="REFab", timing_OK=True, ready=True)
