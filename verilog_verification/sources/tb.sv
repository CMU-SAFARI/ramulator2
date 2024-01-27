// MICRON TECHNOLOGY, INC. - CONFIDENTIAL AND PROPRIETARY INFORMATION
// /****************************************************************************************
// *
// *    File Name:  tb.sv
// *
// * Dependencies:
// *
// *  Description:  Micron SDRAM DDR4 (Double Data Rate 4) test bench
// *
// *         Note: -Set simulator resolution to "ps" accuracy
// *               -Set Debug = 0 to disable $display messages
// *
// *   Disclaimer   This software code and all associated documentation, comments or other
// *  of Warranty:  information (collectively "Software") is provided "AS IS" without
// *                warranty of any kind. MICRON TECHNOLOGY, INC. ("MTI") EXPRESSLY
// *                DISCLAIMS ALL WARRANTIES EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// *                TO, NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND ANY IMPLIED WARRANTIES
// *                OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE. MTI DOES NOT
// *                WARRANT THAT THE SOFTWARE WILL MEET YOUR REQUIREMENTS, OR THAT THE
// *                OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE.
// *                FURTHERMORE, MTI DOES NOT MAKE ANY REPRESENTATIONS REGARDING THE USE OR
// *                THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS,
// *                ACCURACY, RELIABILITY, OR OTHERWISE. THE ENTIRE RISK ARISING OUT OF USE
// *                OR PERFORMANCE OF THE SOFTWARE REMAINS WITH YOU. IN NO EVENT SHALL MTI,
// *                ITS AFFILIATED COMPANIES OR THEIR SUPPLIERS BE LIABLE FOR ANY DIRECT,
// *                INDIRECT, CONSEQUENTIAL, INCIDENTAL, OR SPECIAL DAMAGES (INCLUDING,
// *                WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION,
// *                OR LOSS OF INFORMATION) ARISING OUT OF YOUR USE OF OR INABILITY TO USE
// *                THE SOFTWARE, EVEN IF MTI HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
// *                DAMAGES. Because some jurisdictions prohibit the exclusion or
// *                limitation of liability for consequential or incidental damages, the
// *                above limitation may not apply to you.
// *
// *                Copyright 2003 Micron Technology, Inc. All rights reserved.
// *
// ****************************************************************************************/

`timescale 1ps / 1ps

`include "trace_config.vh"

`include "arch_defines.v"
`include "StateTable.svp"
module tb;
    timeunit 1ps;
    timeprecision 1ps;
    import arch_package::*;
    import proj_package::*;
    StateTable _state();
    `ifdef DDR4_2G
        parameter UTYPE_density CONFIGURED_DENSITY = _2G;
    `elsif DDR4_4G
        parameter UTYPE_density CONFIGURED_DENSITY = _4G;
    `elsif DDR4_8G
        parameter UTYPE_density CONFIGURED_DENSITY = _8G;
    `elsif DDR4_16G
        parameter UTYPE_density CONFIGURED_DENSITY = _16G;
    `endif
    `ifdef DUAL_RANK
        parameter int CONFIGURED_RANKS = 2;
    `else
        parameter int CONFIGURED_RANKS = 1;
    `endif
    `ifdef DDR4_X4
        parameter int CONFIGURED_DQ_BITS = 4;
        DDR4_if #(.CONFIGURED_DQ_BITS(4)) iDDR4();
    `endif
    `ifdef DDR4_X8
        parameter int CONFIGURED_DQ_BITS = 8;
        DDR4_if #(.CONFIGURED_DQ_BITS(8)) iDDR4();
    `endif
    `ifdef DDR4_X16
        parameter int CONFIGURED_DQ_BITS = 16;
        DDR4_if #(.CONFIGURED_DQ_BITS(16)) iDDR4();
    `endif
    `include "timing_tasks.sv"
    reg clk_val, clk_enb;
    bit[159:0] func_str;
    UTYPE_dutconfig _dut_config;
    DDR4_cmd active_cmd = new();
    UTYPE_TimingParameters timing;
    wire model_enable;
    reg model_enable_val;
    wire odt_wire;
    UTYPE_cmdtype driving_cmd;

    integer cycle_ctr;

    // DQ transmit
    reg dq_en;
    reg dqs_en;
    reg[MAX_DQ_BITS-1:0] dq_out;
    reg[MAX_DQS_BITS-1:0] dqs_out;
    reg[MAX_DM_BITS-1:0] dm_out;
    assign iDDR4.DM_n = dq_en ? dm_out : {MAX_DM_BITS{1'bz}};
    assign iDDR4.DQ = dq_en ? dq_out : {MAX_DQ_BITS{1'bz}};
    assign iDDR4.DQS_t = dqs_en ? dqs_out : {MAX_DQS_BITS{1'bz}};
    assign iDDR4.DQS_c = dqs_en ? ~dqs_out : {MAX_DQS_BITS{1'bz}};

    assign model_enable = model_enable_val;

    // DQ receive
    reg[MAX_DM_BITS-1:0] dm_fifo [4*(MAX_RL+MAX_BURST_LEN+MAX_CAL):0];
    reg[MAX_DQ_BITS-1:0] dq_fifo [4*(MAX_RL+MAX_BURST_LEN+MAX_CAL):0];
    wire[MAX_DQ_BITS-1:0] q0, q1, q2, q3;
    reg ptr_rst_n;
    reg[1:0] burst_cntr;

    reg odt_out;
    reg[MAX_RL:0] odt_fifo;

    initial begin
        $timeformat (-9, 1, " ns", 1);
        _dut_config.by_mode = CONFIGURED_DQ_BITS;
        _dut_config.density = CONFIGURED_DENSITY;
        _dut_config.ranks = CONFIGURED_RANKS;
        arch_package::dut_config_populate(_dut_config);
        proj_package::project_dut_config(_dut_config);
        _state.Initialize("tb", _dut_config, 0);
        iDDR4.CK <= 2'b01;
        clk_enb <= 1'b1;
        clk_val <= 1'b1;
        odt_fifo <= 0;
        model_enable_val = 1;
    end

    always @(active_cmd.cmd) begin
        driving_cmd = active_cmd.cmd;
    end
    // Component instantiation
    ddr4_model #(.CONFIGURED_DQ_BITS(CONFIGURED_DQ_BITS), .CONFIGURED_DENSITY(CONFIGURED_DENSITY), .CONFIGURED_RANKS(CONFIGURED_RANKS))
                 golden_model(.model_enable(model_enable), .iDDR4(iDDR4));
    // Clock generator
    always @(posedge clk_val && clk_enb) begin
      clk_val <= #(timing.tCK/2) 1'b0;
      clk_val <= #(timing.tCK) 1'b1;
      iDDR4.CK[1] <= #(timing.tCK/2) 1'b0;
      iDDR4.CK[1] <= #(timing.tCK) 1'b1;
      iDDR4.CK[0] <= #(timing.tCK/2) 1'b1;
      iDDR4.CK[0] <= #(timing.tCK) 1'b0;
      cycle_ctr <= cycle_ctr + 1;
    end

    UTYPE_DutModeConfig _dut_mode_config;


    task power_up();
        func_str = "power_up";
        iDDR4.CS_n = 1'b0;
        iDDR4.ACT_n <= 1;
        iDDR4.RAS_n_A16 <= 1;
        iDDR4.CAS_n_A15 <= 1;
        iDDR4.WE_n_A14 <= 1;
        iDDR4.CKE <= 0;
        iDDR4.TEN <= 0;
        iDDR4.PARITY <= 0;
        iDDR4.ALERT_n <= 1;
        iDDR4.RESET_n <= 0;
        iDDR4.ZQ <= 0;
        iDDR4.PWR <= 0;
        iDDR4.VREF_CA <= 0;
        iDDR4.VREF_DQ <= 0;
        #(timing.tRESET);
        iDDR4.PWR <= 1;
        iDDR4.VREF_CA <= 1;
        iDDR4.VREF_DQ <= 1;
        #(timing.tPWRUP);
        iDDR4.RESET_n <= 1;

        #(timing.tRESETCKE);
        deselect(timing.tPDc);
        odt_out <= 1'b0;
        // After CKE is registered HIGH and after tXPR has been satisfied, MRS commands may be issued.
        @(negedge clk_val) deselect(timing.tXPR/timing.tCK);
        active_cmd.cmd = cmdPDX;
        active_cmd.tCK = timing.tCK;
        _state.UpdateTable(active_cmd);
    endtask

    task load_mode(reg[MAX_BANK_GROUP_BITS-1:0] bg,
                   reg[MAX_BANK_BITS-1:0] ba,
                   reg[MODEREG_BITS-1:0] addr);
        func_str = "load_mode";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b0;
        iDDR4.CAS_n_A15 <= 1'b0;
        iDDR4.WE_n_A14  <= 1'b0;
        iDDR4.BG <= bg;
        iDDR4.BA <= ba;
        iDDR4.ADDR <= addr;
        active_cmd.Populate(cmdLMR, '0, bg, ba, addr[13:0], timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task refresh(reg[MAX_RANK_BITS-1:0] rank = '0, reg[MAX_BANK_GROUP_BITS-1:0] bg = 0, reg[MAX_BANK_BITS-1:0] ba = 0);
        $display("Cycle=%d, Refresh: rank=%d", cycle_ctr, rank);
        func_str = "refresh";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b0;
        iDDR4.CAS_n_A15 <= 1'b0;
        iDDR4.WE_n_A14  <= 1'b1;
        iDDR4.BG  <= bg;
        iDDR4.BA  <= ba;
        iDDR4.C <= rank;
        active_cmd.Populate(cmdREF, rank, bg, ba, iDDR4.ADDR, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task precharge(reg[MAX_RANK_BITS-1:0] rank = '0,
                   reg[MAX_BANK_GROUP_BITS-1:0] bg = '0,
                   reg[MAX_BANK_BITS-1:0] ba = '0,
                   bit ap = 0); // Precharge all
        $display("Cycle=%d, Precharge: rank=%d bg=%d ba=%d ap=%d", cycle_ctr, rank, bg, ba, ap);
        func_str = "precharge";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b0;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b0;
        iDDR4.BG  <= bg;
        iDDR4.BA  <= ba;
        iDDR4.ADDR <= (ap<<10);
        iDDR4.C <= rank;
        active_cmd.Populate(cmdPRE, rank, bg, ba, (ap<<AUTOPRECHARGEADDR), timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task activate(reg[MAX_RANK_BITS-1:0] rank = '0,
                  reg[MAX_BANK_GROUP_BITS-1:0] bg = '0,
                  reg[MAX_BANK_BITS-1:0] ba = '0,
                  reg[MAX_ROW_ADDR_BITS-1:0] row = '0);
        $display("Cycle=%d, Activate: rank=%d bg=%d ba=%d row=%d", cycle_ctr, rank, bg, ba, row);
        func_str = "activate";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n <= 1'b0;
        iDDR4.ACT_n <= 1'b0;
        iDDR4.RAS_n_A16 <= row[16];
        iDDR4.CAS_n_A15 <= row[15];
        iDDR4.WE_n_A14 <= row[14];
        iDDR4.BG <= bg;
        iDDR4.BA <= ba;
        iDDR4.ADDR <= row;
        iDDR4.C <= rank;
        active_cmd.Populate(cmdACT, rank, bg, ba, row, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    // Write task supports burst lengths <= 8
    task write(reg[MAX_RANK_BITS-1:0] rank = '0,
               reg[MAX_BANK_GROUP_BITS-1:0] bg = '0,
               reg[MAX_BANK_BITS-1:0] ba = '0,
               reg[MAX_COL_ADDR_BITS-1:0] col = '0,
               bit ap = 0, // Auto Precharge
               bit bc = 0, // Burst Chop
               reg[MAX_BURST_LEN*MAX_DM_BITS-1:0] dm = '0,
               reg[MAX_BURST_LEN*MAX_DQ_BITS-1:0] dq = '0
               );
        int wl, bl;
        reg[MAX_ADDR_BITS-1:0] drive_addr[2:0];

        $display("Cycle=%d, Write: rank=%d bg=%d ba=%d col=%d ap=%d", cycle_ctr, rank, bg, ba, col, ap);
        func_str = "write";
        drive_addr[0] = col & _dut_config.col_mask; // addr[9:0] = COL[9:0]
        drive_addr[1] = ((col >> 10) & 1'h1) << 11; // addr[11] = COL[10]
        drive_addr[2] = (col >> 11) << 13;         // addr[N:13] = COL[N:11]
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b0;
        iDDR4.WE_n_A14  <= 1'b0;
        iDDR4.BG  <= bg;
        iDDR4.BA  <= ba;
        iDDR4.ADDR <= drive_addr[0] | drive_addr[1] | drive_addr[2] |
                      (ap << AUTOPRECHARGEADDR) | (bc << BLFLYSELECT);
        iDDR4.C <= rank;

        wl = _dut_mode_config.WL_calculated;
        bl = _state.CheckFlyBL(bc << BLFLYSELECT);
        dqs_en <= #(wl*timing.tCK-(timing.tCK*_dut_mode_config.wr_preamble_clocks)) 1'b1;
        dqs_out <= #(wl*timing.tCK-(timing.tCK*_dut_mode_config.wr_preamble_clocks)) {MAX_DQS_BITS{1'b1}};
        for (int i=0; i<=bl; i++) begin
            dqs_en <= #(wl*timing.tCK + i*timing.tCK/2) 1'b1;
            if (i%2 == 0) begin
                dqs_out <= #(wl*timing.tCK + i*timing.tCK/2) {MAX_DQS_BITS{1'b0}};
            end else begin
                dqs_out <= #(wl*timing.tCK + i*timing.tCK/2) {MAX_DQS_BITS{1'b1}};
            end

            dq_en  <= #(wl*timing.tCK + i*timing.tCK/2 + timing.tCK/4) 1'b1;
            dm_out <= #(wl*timing.tCK + i*timing.tCK/2 + timing.tCK/4) dm>>i*MAX_DM_BITS;
            dq_out <= #(wl*timing.tCK + i*timing.tCK/2 + timing.tCK/4) dq>>i*MAX_DQ_BITS;
        end
        dqs_en <= #(wl*timing.tCK + bl*timing.tCK/2 + timing.tCK/2) 1'b0;
        dq_en  <= #(wl*timing.tCK + bl*timing.tCK/2 + timing.tCK/4) 1'b0;
        active_cmd.Populate(cmdWR, rank, bg, ba, col, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    // Read without data verification
    task read(reg[MAX_RANK_BITS-1:0] rank = '0,
              reg[MAX_BANK_GROUP_BITS-1:0] bg = '0,
              reg[MAX_BANK_BITS-1:0] ba = '0,
              reg[MAX_COL_ADDR_BITS-1:0] col = '0,
              bit ap = 0, // Auto Precharge
              bit bc = 0 // Burst Chop
             );
        int wl, rl, bl;
        reg[MAX_ADDR_BITS-1:0] drive_addr[2:0];

        $display("Cycle=%d, Read: rank=%d bg=%d ba=%d col=%d ap=%d", cycle_ctr, rank, bg, ba, col, ap);
        func_str = "read";
        drive_addr[0] = col & _dut_config.col_mask; // addr[9:0] = COL[9:0]
        drive_addr[1] = ((col >> 10) & 1'h1) << 11; // addr[11] = COL[10]
        drive_addr[2] = (col >> 11) << 13;         // addr[N:13] = COL[N:11]
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b0;
        iDDR4.WE_n_A14  <= 1'b1;
        iDDR4.BG  <= bg;
        iDDR4.BA  <= ba;
        iDDR4.ADDR <= drive_addr[0] | drive_addr[1] | drive_addr[2] |
                      (ap << AUTOPRECHARGEADDR) | (bc << BLFLYSELECT);
        iDDR4.C <= rank;

        wl = _dut_mode_config.WL_calculated;
        rl = _dut_mode_config.RL;
        bl = _state.CheckFlyBL(bc << BLFLYSELECT);

        for (int i=0; i<(bl/2 + 2); i=i+1) begin
            odt_fifo[rl-wl + i] = 1'b1;
        end
        active_cmd.Populate(cmdRD, rank, bg, ba, col, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task zq_cs(reg[MAX_ADDR_BITS-1:0] addr = '0);
        func_str = "zq_cs";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b0;
        addr = addr & ~(1'b1 << AUTOPRECHARGEADDR);
        iDDR4.ADDR <= addr;
        active_cmd.Populate(cmdZQ, iDDR4.C, iDDR4.BG, iDDR4.BA, addr, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task zq_cl(reg[MAX_ADDR_BITS-1:0] addr = '0);
        func_str = "zq_cs";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b0;
        addr = addr | (1'b1 << AUTOPRECHARGEADDR);
        iDDR4.ADDR <= addr;
        active_cmd.Populate(cmdZQ, iDDR4.C, iDDR4.BG, iDDR4.BA, addr, timing.tCK);
        _state.UpdateTable(active_cmd);
        @(negedge clk_val);
    endtask

    task nop(int count);
        func_str = "nop";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b0;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b1;
        active_cmd.cmd = cmdNOP;
        active_cmd.tCK = timing.tCK;
        repeat(count) _state.UpdateTable(active_cmd);
        repeat(count) @(negedge clk_val);
    endtask

    task deselect(int count);
        func_str = "deselect";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b1;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b1;
        active_cmd.cmd = cmdDES;
        active_cmd.tCK = timing.tCK;
        repeat(count) _state.UpdateTable(active_cmd);
        repeat(count) @(negedge clk_val);
    endtask

    task start_trace();
        func_str = "start_trace";
        iDDR4.CKE <= 1'b1;
        iDDR4.CS_n  <= 1'b1;
        iDDR4.ACT_n <= 1'b1;
        iDDR4.RAS_n_A16 <= 1'b1;
        iDDR4.CAS_n_A15 <= 1'b1;
        iDDR4.WE_n_A14  <= 1'b1;
        cycle_ctr = 0;
        active_cmd.cmd = cmdDES;
        active_cmd.tCK = timing.tCK;
        @(negedge clk_val);
    endtask

    task default_period(UTYPE_TS new_ts);
        SetTimingStruct(new_ts);
        timing.tCK = tCK;
        _state.UpdateTiming(timing);
        $display("%m at time %0t: INFO: Default Clock Period to %s (%0d ps)", $time, new_ts.name(), timing.tCK);
        @(negedge clk_val);
        #(timing.tPD);
    endtask

    // End-of-test triggered in 'subtest.vh'
    task test_done;
        begin
            $display ("%m at time %0t: INFO: Simulation is Complete", $time);
            $finish(0);
        end
    endtask

    function int GetWidth();
        return _dut_config.by_mode;
    endfunction

    // Test included from external file
    `include "subtest.vh"

endmodule
