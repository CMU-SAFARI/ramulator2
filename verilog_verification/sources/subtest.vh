/****************************************************************************************
*
*    File Name:  subtest.vh
*
*  Description:  Micron SDRAM DDR4 (Double Data Rate 4)
*                This file is included by tb.v
*
*   Disclaimer   This software code and all associated documentation, comments or other
*  of Warranty:  information (collectively "Software") is provided "AS IS" without
*                warranty of any kind. MICRON TECHNOLOGY, INC. ("MTI") EXPRESSLY
*                DISCLAIMS ALL WARRANTIES EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
*                TO, NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND ANY IMPLIED WARRANTIES
*                OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE. MTI DOES NOT
*                WARRANT THAT THE SOFTWARE WILL MEET YOUR REQUIREMENTS, OR THAT THE
*                OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.
*                FURTHERMORE, MTI DOES NOT MAKE ANY REPRESENTATIONS REGARDING THE USE OR
*                THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS,
*                ACCURACY, RELIABILITY, OR OTHERWISE. THE ENTIRE RISK ARISING OUT OF USE
*                OR PERFORMANCE OF THE SOFTWARE REMAINS WITH YOU. IN NO EVENT SHALL MTI,
*                ITS AFFILIATED COMPANIES OR THEIR SUPPLIERS BE LIABLE FOR ANY DIRECT,
*                INDIRECT, CONSEQUENTIAL, INCIDENTAL, OR SPECIAL DAMAGES (INCLUDING,
*                WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION,
*                OR LOSS OF INFORMATION) ARISING OUT OF YOUR USE OF OR INABILITY TO USE
*                THE SOFTWARE, EVEN IF MTI HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
*                DAMAGES. Because some jurisdictions prohibit the exclusion or
*                limitation of liability for consequential or incidental damages, the
*                above limitation may not apply to you.
*
*                Copyright 2003 Micron Technology, Inc. All rights reserved.
*
****************************************************************************************/

    initial begin : test
        UTYPE_TS nominal_ts;
        reg [MODEREG_BITS-1:0] mode_regs[MAX_MODEREGS];
        UTYPE_DutModeConfig dut_mode_config;
        `ifdef DDR4_1600
            nominal_ts = TS_1250;
        `endif
        `ifdef DDR4_1866
            nominal_ts = TS_1072;
        `endif
        `ifdef DDR4_2133
            nominal_ts = TS_938;
        `endif
        `ifdef DDR4_2400
            nominal_ts = TS_833;
        `endif
        `ifdef DDR4_2666
            nominal_ts = TS_750;
        `endif
        `ifdef DDR4_2933
            nominal_ts = TS_682;
        `endif
        `ifdef DDR4_3200
            nominal_ts = TS_625;
        `endif
        iDDR4.RESET_n <= 1'b1;
        iDDR4.CKE <= 1'b0;
        iDDR4.CS_n  <= 1'b1;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14 <= 1'b1;
        iDDR4.BG <= '1;
        iDDR4.BA <= '1;
        iDDR4.ADDR <= '1;
        iDDR4.ADDR_17 <= '0;
        iDDR4.ODT <= 1'b0;
        iDDR4.PARITY <= 0;
        iDDR4.ALERT_n <= 1;
        iDDR4.PWR <= 0;
        iDDR4.TEN <= 0;
        iDDR4.VREF_CA <= 0;
        iDDR4.VREF_DQ <= 0;
        iDDR4.ZQ <= 0;
        dq_en <= 1'b0;
        dqs_en <= 1'b0;

        default_period(nominal_ts);

        // POWERUP SECTION
        power_up();

        // Reset DLL
        dut_mode_config = _state.DefaultDutModeConfig(.cl(timing.cl),
                                                      .write_recovery(timing.tWRc),
                                                      .qoff(0),
                                                      .cwl(timing.cwl),
                                                      .wr_preamble_clocks(1),
                                                      .bl_reg(rBL8),
                                                      .dll_enable(1),
                                                      .dll_reset(1),
                                                      .tCCD_L(timing.tCCDc_L));
        _state.ModeToAddrDecode(dut_mode_config, mode_regs);
        load_mode(.bg(0), .ba(1), .addr(mode_regs[1]));
        deselect(timing.tDLLKc);

        dut_mode_config.DLL_reset = 0;
        _state.ModeToAddrDecode(dut_mode_config, mode_regs);

        load_mode(.bg(0), .ba(3), .addr(mode_regs[3]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(1), .ba(2), .addr(mode_regs[6]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(1), .ba(1), .addr(mode_regs[5]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(1), .ba(0), .addr(mode_regs[4]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(0), .ba(2), .addr(mode_regs[2]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(0), .ba(1), .addr(mode_regs[1]));
        deselect(timing.tMOD/timing.tCK);
        load_mode(.bg(0), .ba(0), .addr(mode_regs[0]));
        deselect(timing.tMOD/timing.tCK);
        zq_cl();
        deselect(timing.tZQinitc);

        odt_out <= 1;                           // turn on odt

        deselect(100);
        start_trace();
        $display("========================================");
        $display("Configurations");
        $display("DDR Speed DDR4_%0s", timing.timing_name);
        $display("Clock Period %s (%0d ps)", nominal_ts.name(), timing.tCK);
        $display("Model configured as density=%0dG, rank=%0d, dq_pins=%0d", CONFIGURED_DENSITY, CONFIGURED_RANKS, CONFIGURED_DQ_BITS);
        $display("----------------------------------------");
        $display("nCL=%0d", timing.cl);
        $display("nRCD=%0d", timing.tRCDc);
        $display("nRP=%0d", timing.tRPc);
        $display("nRAS=%0d", timing.tRASc);
        $display("nRC=%0d", timing.tRCc);
        $display("nWR=%0d", timing.tWRc);
        $display("nRTP=%0d", timing.tRTPc);
        $display("nCWL=%0d", timing.cwl);
        $display("nCCD_S=%0d", timing.tCCDc_S);
        $display("nCCD_L=%0d", timing.tCCDc_L);
        $display("nRRD_S=%0d", timing.tRRDc_S);
        $display("nRRD_L=%0d", timing.tRRDc_L);
        $display("nWTR_S=%0d", timing.tWTRc_S);
        $display("nWTR_L=%0d", timing.tWTRc_L);
        $display("nFAW=%0d", timing.tFAWc);
        $display("tRFC1=%0d", timing.tRFC1);
        $display("tRFC2=%0d", timing.tRFC2);
        $display("tRFC4=%0d", timing.tRFC4);
        $display("nRFC1=%0d", timing.tRFC1c);
        $display("nRFC2=%0d", timing.tRFC2c);
        $display("nRFC4=%0d", timing.tRFC4c);
        $display("----------------------------------------");

        $display("Trace Starts", $time);


        // load trace tb file
        `include "trace_tb.v"

        deselect(100);
        test_done;
    end
