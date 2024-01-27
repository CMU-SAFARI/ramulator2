// MICRON TECHNOLOGY, INC. - CONFIDENTIAL AND PROPRIETARY INFORMATION
interface DDR4_if #(parameter CONFIGURED_DQ_BITS = 8) ();
    timeunit 1ps;
    timeprecision 1ps;
    import arch_package::*;
    parameter CONFIGURED_DQS_BITS = (16 == CONFIGURED_DQ_BITS) ? 2 : 1;
    parameter CONFIGURED_DM_BITS = (16 == CONFIGURED_DQ_BITS) ? 2 : 1;
    logic[1:0] CK; // CK[0]==CK_c CK[1]==CK_t
    logic ACT_n;
    logic RAS_n_A16;
    logic CAS_n_A15;
    logic WE_n_A14;
    logic ALERT_n;
    logic PARITY;
    logic RESET_n;
    logic TEN;
    logic CS_n;
    logic CKE;
    logic ODT;
    logic[MAX_RANK_BITS-1:0] C;
    logic[MAX_BANK_GROUP_BITS-1:0] BG;
    logic[MAX_BANK_BITS-1:0] BA;
    logic[13:0] ADDR;
    logic ADDR_17;
    wire[CONFIGURED_DM_BITS-1:0] DM_n;
    wire[CONFIGURED_DQ_BITS-1:0] DQ;
    wire[CONFIGURED_DQS_BITS-1:0] DQS_t;
    wire[CONFIGURED_DQS_BITS-1:0] DQS_c;
    logic ZQ;
    logic PWR;
    logic VREF_CA;
    logic VREF_DQ;
endinterface

