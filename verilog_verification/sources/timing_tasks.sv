// MICRON TECHNOLOGY, INC. - CONFIDENTIAL AND PROPRIETARY INFORMATION
typedef enum {
    TS_LOADED, 
    itCK_min, itCK_max,
    itDQSQ, itQHp, itDS, itDH, itIPW,
    itRPREp, itRPSTp, itQSHp, itQSLp,
    itWPSTp, itWPREp, itDQSCK, itDQSCK_min, itDQSCK_max,
    itDQSLp, itDQSHp, itDQSSp, itDQSSp_min, itDQSSp_max,
    itDLLKc_min, itRTP, itRTPc, itWTRc_S, itWTRc_L, itWTRc_S_CRC_DM, itWTRc_L_CRC_DM, itWR, itWRc, itMRDc, itMOD, itMODc,
    itRCDc, itRPc, itRP_ref, itRCc, itCCDc_S, itCCDc_L,
    itRRDc_S_512, itRRDc_L_512, itRRDc_S_1k, itRRDc_L_1k, itRRDc_S_2k, itRRDc_L_2k,
    // itRRD_S_512, itRRD_L_512, itRRD_S_1k, itRRD_L_1k, itRRD_S_2k, itRRD_L_2k,
    itRASc, itFAWc_512, itFAWc_1k, itFAWc_2k, itIS, itIH, itDIPW,
    itXPR,
    itPAR_ALERT_PWc, itPAR_ALERT_PWc_min, itPAR_ALERT_PWc_max,
    itXS, itXSDLLc, itCKESR,
    itCKSREc, itCKSRE, itCKSRXc, itCKSRX, itXSR,
    itXP, itXPc, itXPDLL, itXPDLLc, itCKE, itCKEc, itCPDEDc, itPD, itPDc,
    itACTPDENc, itPREPDENc, itREFPDENc, itMRSPDEN,
    itZQinitc, itZQoperc, itZQCSc,
    itWLS, itWLH, itAON_min, itAON_max,
    iCLc, iCWLc,
    NUM_TIMESPEC
} UTYPE_time_spec;

// timing definition in tCK units
integer tt_timesets [0:NUM_TS-1][0:NUM_TIMESPEC-1];
bit[159:0] tt_timenames [0:NUM_TS-1];
integer tCK;
integer tOffset;
int max_tCK, min_tCK;

initial begin
    LoadTiming();
end

// tCK and tOffset are the 'master' values.
always @(tCK) begin
    timing.tCK = tCK;
end
always @(tOffset) begin
    timing.tOffset = tOffset;
end

task SettCK(input integer _tCK);
    tCK = _tCK;
    tOffset = _tCK;
endtask

task SetTimingStruct(input UTYPE_TS ts, UTYPE_tCKMode tck_mode = TCK_MIN);
    if (tt_timesets[ts][TS_LOADED] != 1) begin
        UTYPE_TS temp_ts; // 6.2 does not support name() on structures.

        temp_ts = timing.ts_loaded;
        $display("SetTiming:ERROR: Invalid timeset requested '%0s' @%0t. Timeset stays at '%0s'.",
                    ts.name(), $time, temp_ts.name());
        return;
    end

    if(tck_mode === TCK_MAX) begin
        SettCK(tt_timesets[ts][itCK_max]);
    end else if(tck_mode === TCK_RANDOM) begin
        integer rand_tckmode;
        rand_tckmode = $urandom % 100;
        if (rand_tckmode < 50) // 50%
            SettCK($urandom_range(tt_timesets[ts][itCK_min], tt_timesets[ts][itCK_max]));
        else if (rand_tckmode < 75) // 25%
            SettCK(tt_timesets[ts][itCK_max]);
        else // 25%
            SettCK(tt_timesets[ts][itCK_min]);
    end else
        SettCK(tt_timesets[ts][itCK_min]);
    `ifndef SILENT
    $display("Setting Timeset:%0s with tCk:%0d @%0t", ts.name(), tCK, $time);
    `endif
    timing.ts_loaded = ts;
    timing.timing_name = tt_timenames[ts];
    timing.tck_mode = tck_mode;
    // Ramulator Parameters
    timing.tCK_min = tt_timesets[ts][itCK_min];
    
    timing.cl = tt_timesets[ts][iCLc];

    timing.tRCD = tt_timesets[ts][itRCDc] * tt_timesets[ts][itCK_min];
    timing.tRCDc = tt_timesets[ts][itRCDc];
    timing.tRP = tt_timesets[ts][itRPc] * tt_timesets[ts][itCK_min];
    timing.tRPc = tt_timesets[ts][itRPc];
    timing.tRAS = tt_timesets[ts][itRASc] * tt_timesets[ts][itCK_min];
    timing.tRASc = tt_timesets[ts][itRASc];
    timing.tRC = tt_timesets[ts][itRCc] * tt_timesets[ts][itCK_min];
    timing.tRCc = tt_timesets[ts][itRCc];
    timing.tWR = tt_timesets[ts][itWRc] * tt_timesets[ts][itCK_min];
    timing.tWRc = tt_timesets[ts][itWRc];
    timing.tRTP = tt_timesets[ts][itRTPc] * tt_timesets[ts][itCK_min];
    timing.tRTPc = tt_timesets[ts][itRTPc];

    timing.cwl = tt_timesets[ts][iCWLc];
    
    timing.tCCD_S = tt_timesets[ts][itCCDc_S] * tt_timesets[ts][itCK_min];
    timing.tCCDc_S = tt_timesets[ts][itCCDc_S];
    timing.tCCD_L = tt_timesets[ts][itCCDc_L] * tt_timesets[ts][itCK_min];
    timing.tCCDc_L = tt_timesets[ts][itCCDc_L];
    if (16 == GetWidth()) begin
        timing.tFAW = tt_timesets[ts][itFAWc_2k] * tt_timesets[ts][itCK_min];
        timing.tFAWc = tt_timesets[ts][itFAWc_2k];
        timing.tRRDc_S = tt_timesets[ts][itRRDc_S_2k];
        timing.tRRDc_L = tt_timesets[ts][itRRDc_L_2k];
        timing.tRRD_S = tt_timesets[ts][itRRDc_S_2k] * tt_timesets[ts][itCK_min];
        timing.tRRD_L = tt_timesets[ts][itRRDc_L_2k] * tt_timesets[ts][itCK_min];
    end else if (4 == GetWidth()) begin
        timing.tFAW = tt_timesets[ts][itFAWc_512] * tt_timesets[ts][itCK_min];
        timing.tFAWc = tt_timesets[ts][itFAWc_512];
        timing.tRRDc_S = tt_timesets[ts][itRRDc_S_512];
        timing.tRRDc_L = tt_timesets[ts][itRRDc_L_512];
        timing.tRRD_S = tt_timesets[ts][itRRDc_S_512] * tt_timesets[ts][itCK_min];
        timing.tRRD_L = tt_timesets[ts][itRRDc_L_512] * tt_timesets[ts][itCK_min];
    end else begin
        timing.tFAW = tt_timesets[ts][itFAWc_1k] * tt_timesets[ts][itCK_min];
        timing.tFAWc = tt_timesets[ts][itFAWc_1k];
        timing.tRRDc_S = tt_timesets[ts][itRRDc_S_1k];
        timing.tRRDc_L = tt_timesets[ts][itRRDc_L_1k];
        timing.tRRD_S = tt_timesets[ts][itRRDc_S_1k] * tt_timesets[ts][itCK_min];
        timing.tRRD_L = tt_timesets[ts][itRRDc_L_1k] * tt_timesets[ts][itCK_min];
    end
    timing.tWTR_S = tt_timesets[ts][itWTRc_S] * tt_timesets[ts][itCK_min];
    timing.tWTRc_S = tt_timesets[ts][itWTRc_S];
    timing.tWTR_L = tt_timesets[ts][itWTRc_L] * tt_timesets[ts][itCK_min];
    timing.tWTRc_L = tt_timesets[ts][itWTRc_L];
    
    timing.tRTP_min = tt_timesets[ts][itRTPc] * tt_timesets[ts][itCK_min];
    timing.tRTPc_min = tt_timesets[ts][itRTPc];
    if (_2G == CONFIGURED_DENSITY) begin
        timing.tRFC1 = 160000;
        timing.tRFC2 = 110000;
        timing.tRFC4 = 90000;
    end else if (_4G == CONFIGURED_DENSITY) begin
        timing.tRFC1 = 260000;
        timing.tRFC2 = 160000;
        timing.tRFC4 = 110000;
    end else if (_8G == CONFIGURED_DENSITY) begin
        timing.tRFC1 = 360000;
        timing.tRFC2 = 260000;
        timing.tRFC4 = 160000;
    end else if (_16G == CONFIGURED_DENSITY) begin
        timing.tRFC1 = 550000;
        timing.tRFC2 = 350000;
        timing.tRFC4 = 260000;
    end
    timing.tRFC = timing.tRFC1;
    timing.tRFCc = ParamInClks(timing.tRFC1, tt_timesets[ts][itCK_min]);
    timing.tRFC1c = ParamInClks(timing.tRFC1, tt_timesets[ts][itCK_min]);
    timing.tRFC2c = ParamInClks(timing.tRFC2, tt_timesets[ts][itCK_min]);
    timing.tRFC4c = ParamInClks(timing.tRFC4, tt_timesets[ts][itCK_min]);




    // Clock timing.
    timing.ClockDutyCycle = 50;
    timing.tCK_max = tt_timesets[ts][itCK_max];
    timing.tCHp_min = 48;
    timing.tCHp_min = 52;
    // Data timing.
    timing.tDQSQ = tt_timesets[ts][itDQSQ];
    timing.tQHp = 38;
    timing.tDS = tt_timesets[ts][itDS];
    timing.tDH = tt_timesets[ts][itDH];
    timing.tIPW = tt_timesets[ts][itIPW];
    // Data strobe timing.
    if (0 < tt_timesets[ts][itRPREp])
        timing.tRPREp = tt_timesets[ts][itRPREp];
    else
        timing.tRPREp = 100;
    if (0 < tt_timesets[ts][itRPSTp])
        timing.tRPSTp = tt_timesets[ts][itRPSTp];
    else
        timing.tRPSTp = 50;
    timing.tQSHp = 40;
    timing.tQSLp = 40;
    timing.tWPSTp = 50;
    timing.tWPREp = 100;
    timing.tDQSCK = tt_timesets[ts][itDQSCK];
    timing.tDQSCK_dll_on = tt_timesets[ts][itDQSCK];
    timing.tDQSCK_min = tt_timesets[ts][itDQSCK_min];
    timing.tDQSCK_max = tt_timesets[ts][itDQSCK_max];
    timing.tDQSCK_dll_off = 5800;
    timing.tDQSCK_dll_off_min = 1000;
    timing.tDQSCK_dll_off_max = 9000;
    timing.tDQSLp = 50;
    timing.tDQSLp_min = 45;
    timing.tDQSLp_max = 55;
    timing.tDQSHp = 50;
    timing.tDQSHp_min = 45;
    timing.tDQSHp_max = 55;
    timing.tDQSSp = 0; // Nominal.
    timing.tDQSSp_1tCK_min = -27;
    timing.tDQSSp_1tCK_max = 27;
    timing.tDQSSp_2tCK_min = timing.tDQSSp_1tCK_min;
    timing.tDQSSp_2tCK_max = timing.tDQSSp_1tCK_max;
    timing.tDQSSp_min = timing.tDQSSp_1tCK_min;
    timing.tDQSSp_max = timing.tDQSSp_1tCK_max;
    // Command and address timing.
    timing.tDLLKc = tt_timesets[ts][itDLLKc_min];
    timing.tWTR_S_CRC_DM = 3750;
    timing.tWTRc_S_CRC_DM = ParamInClks(tt_timesets[ts][timing.tWTR_S_CRC_DM], tt_timesets[ts][itCK_min]);
    timing.tWTR_L_CRC_DM = 3750;
    timing.tWTRc_L_CRC_DM = ParamInClks(tt_timesets[ts][timing.tWTR_L_CRC_DM], tt_timesets[ts][itCK_min]);
    timing.tWR_CRC_DMc = 5;
    timing.tMRDc = 8;
    timing.tMOD = tt_timesets[ts][itMOD];
    timing.tMODc = ParamInClks(tt_timesets[ts][itMOD], tt_timesets[ts][itCK_min]);
    timing.tMPRRc = 1;
    timing.tWR_MPRc = timing.tMODc;
    timing.tRP_ref_internal = tt_timesets[ts][itRP_ref];
    timing.tRPc_ref_internal = ParamInClks(tt_timesets[ts][itRP_ref], tt_timesets[ts][itCK_min]);
    timing.tPAR_CLOSE_BANKS = timing.tRAS;
    timing.tPAR_ALERT_ON = 1400;
    timing.tPAR_ALERT_ON_max = 6000;
    timing.tPAR_ALERT_ON_CYCLES = 4;
    timing.tPAR_ALERT_OFF = 3000;
    timing.tPAR_tRP_tRAS_adjustment = 2000;
    timing.tPAR_tRP_holdoff_adjustment = 1450;
    timing.tPAR_ALERT_PWc = tt_timesets[ts][itPAR_ALERT_PWc];
    timing.tPAR_ALERT_PWc_min = tt_timesets[ts][itPAR_ALERT_PWc_min];
    timing.tPAR_ALERT_PWc_max = tt_timesets[ts][itPAR_ALERT_PWc_max];
    timing.tPAR_ALERT_PW = timing.tPAR_ALERT_PWc * tt_timesets[ts][itCK_min];
    timing.tPAR_ALERT_PW_min = timing.tPAR_ALERT_PWc_min * tt_timesets[ts][itCK_min];
    timing.tPAR_ALERT_PW_max = timing.tPAR_ALERT_PWc_max * tt_timesets[ts][itCK_min];
    timing.tCRC_ALERT = 9000;
    timing.tCRC_ALERT_min = 3000;
    timing.tCRC_ALERT_max = 13000;
    timing.tCRC_ALERT_PWc_min = 6;
    timing.tCRC_ALERT_PWc_max = 10;
    timing.tCRC_ALERT_PWc = 8;
    timing.tRRDc_dlr = 0; // not defined in ramulator
    timing.tFAWc_dlr = 0;
    timing.tRFCc_dlr = 0;
    timing.tIS = tt_timesets[ts][itIS];
    timing.tIS_CKE = tt_timesets[ts][itIS];
    timing.tIH = tt_timesets[ts][itIH];
    timing.tDIPW = tt_timesets[ts][itDIPW];
    // Reset timing.
    timing.tXPR = timing.tRFC + 10000;
    // Self refresh timing.
    timing.tXS = timing.tRFC + 10000;
    timing.tXSc = ParamInClks(timing.tRFC + 10000, tt_timesets[ts][itCK_min]);
    timing.tXS_Fast = timing.tRFC4 + 10000;
    timing.tXS_Fastc = ParamInClks(timing.tXS_Fast, tt_timesets[ts][itCK_min]);
    timing.tXSDLLc = tt_timesets[ts][itDLLKc_min];
    timing.tCKESRc = ParamInClks(tt_timesets[ts][itCKE], tt_timesets[ts][itCK_min]) + 1;
    timing.tCKSRE = 10000;
    timing.tCKSREc = ParamInClks(timing.tCKSRE, tt_timesets[ts][itCK_min]);
    if (timing.tCKSREc < 5) begin
        timing.tCKSREc = 5;
        timing.tCKSRE = timing.tCKSREc * tt_timesets[ts][itCK_min];
    end
    timing.tCKSRX = 10000;
    timing.tCKSRXc = ParamInClks(timing.tCKSRX, tt_timesets[ts][itCK_min]);
    if (timing.tCKSRXc < 5) begin
        timing.tCKSRXc = 5;
        timing.tCKSRX = timing.tCKSRXc * tt_timesets[ts][itCK_min];
    end
    timing.tXSR = timing.tRFC + 10000;
    // Power down timing.
    if (timing.tCK_min > 1500) begin
        timing.tXPc = 4;
        timing.tXP = 4 * timing.tCK_min;
    end else begin
        timing.tXP = 6000;
        timing.tXPc = ParamInClks(6000, tt_timesets[ts][itCK_min]);
    end
    timing.tXPDLL = tt_timesets[ts][itXPDLL];
    timing.tXPDLLc = ParamInClks(tt_timesets[ts][itXPDLL], tt_timesets[ts][itCK_min]);
    timing.tCKE = tt_timesets[ts][itCKE];
    timing.tCKEc = ParamInClks(tt_timesets[ts][itCKE], tt_timesets[ts][itCK_min]);
    timing.tCPDEDc = tt_timesets[ts][itCPDEDc];
    timing.tPD = tt_timesets[ts][itCKE];
    timing.tPDc = ParamInClks(tt_timesets[ts][itCKE], tt_timesets[ts][itCK_min]);
    // tRDPDEN/tWRPDEN are calculated dynamically.
    timing.tACTPDENc = tt_timesets[ts][itACTPDENc];
    timing.tPREPDENc = tt_timesets[ts][itPREPDENc];
    timing.tREFPDENc = tt_timesets[ts][itREFPDENc];
    timing.tMRSPDENc = timing.tMODc;
    timing.tMRSPDEN = tt_timesets[ts][itMOD];
    // Initialization timing.
    timing.tPWRUP = 100000;
    timing.tRESET = 100000; // Stable power @100ns ramp @200us.
    timing.tRESETCKE = 100000; // Spec is 500us.
    timing.tODTHc = 4;
    timing.tZQinitc = tt_timesets[ts][itZQinitc];
    timing.tZQoperc = tt_timesets[ts][itZQoperc];
    timing.tZQCSc = tt_timesets[ts][itZQCSc];
    timing.tZQRTT = 44_000;
    timing.tZQRTTc = ParamInClks(timing.tZQRTT, tt_timesets[ts][itCK_min]);
    // Write leveling.
    timing.tWLMRDc = 40;
    timing.tWLDQSENc = 25;
    timing.tWLS = tt_timesets[ts][itWLS];
    timing.tWLSc = ParamInClks(tt_timesets[ts][itWLS], tt_timesets[ts][itCK_min]);
    timing.tWLH = tt_timesets[ts][itWLH];
    timing.tWLHc = ParamInClks(tt_timesets[ts][itWLH], tt_timesets[ts][itCK_min]);
    timing.tWLO_min = 0;
    timing.tWLOc_min = 0;
    timing.tWLO_max = 7500;
    timing.tWLOc_max = ParamInClks(timing.tWLO_max, tt_timesets[ts][itCK_min]);
    timing.tWLOE_min = 0;
    timing.tWLOEc_min = 0;
    timing.tWLOEc_max = ParamInClks(timing.tWLOE_max, tt_timesets[ts][itCK_min]);
    timing.tWLO_nominal = (timing.tWLO_min + timing.tWLO_max)/2;
    timing.tWLOE_nominal = (timing.tWLOE_min + timing.tWLOE_max)/2;
    // ODT Timing.
    timing.tAON = 0;
    timing.tAON_min = tt_timesets[ts][itAON_min];
    timing.tAON_max = tt_timesets[ts][itAON_max];
    timing.tAOF = 0;
    timing.tAOFp = 50;
    timing.tAOFp_min = 30;
    timing.tAOFp_max = 70;
    timing.tADCp = 50;
    timing.tADCp_min = 30;
    timing.tADCp_max = 70;
    timing.tAONPD = 5000;
    timing.tAONPDc = 5000/tt_timesets[ts][itCK_min]; // Do not round ntCK up.
    timing.tAONPD_min = 1000;
    timing.tAONPD_max = 10000;
    timing.tAOFPD = 5000;
    timing.tAOFPDc = 5000/tt_timesets[ts][itCK_min]; // Do not round ntCK up.
    timing.tAOFPD_min = 1000;
    timing.tAOFPD_max = 10000;
    timing.tAOFASp = 50; // Async time for ODT forced off (read).
    timing.tAONASp_min = 1000; // Async ODT turn on/off.
    timing.tAONASp_max = 9000;
    timing.tAOFASp_min = 1000;
    timing.tAOFASp_max = 9000;
    // Read preamble training.
    timing.tSDO_max = timing.tMOD + 9000;
    timing.tSDOc_max = ParamInClks(timing.tSDO_max, tt_timesets[ts][itCK_min]);
    timing.tSDO = 7000;
    timing.tSDOc = ParamInClks(timing.tSDO, tt_timesets[ts][itCK_min]);
    // Gear down setup.
    timing.tSYNC_GEARc = (0 == (timing.tMODc % 2)) ? timing.tMODc + 4 : timing.tMODc + 5;
    timing.tCMD_GEARc = (0 == (timing.tMODc % 2)) ? timing.tMODc : timing.tMODc + 1;
    timing.tGD_TRANSITIONc = MAX_WL;
    // Maximum power saving setup.
    timing.tMPED = timing.tMOD + timing.tCPDEDc*tt_timesets[ts][itCK_min];
    timing.tCKMPE = timing.tMOD + timing.tCPDEDc*tt_timesets[ts][itCK_min];
    timing.tCKMPX = timing.tCKSRX;
    timing.tMPX_H = 2*tt_timesets[ts][itCK_min];
    timing.tMPEDc = ParamInClks(timing.tMPED, tt_timesets[ts][itCK_min]);
    timing.tCKMPEc = ParamInClks(timing.tCKMPE, tt_timesets[ts][itCK_min]);
    timing.tCKMPXc = ParamInClks(timing.tCKMPX, tt_timesets[ts][itCK_min]);
    timing.tXMPc = timing.tXSc;
    timing.tXMPDLLc = timing.tXMPc + timing.tXSDLLc;
    timing.tXMP_LHc_max=timing.tXMPc-ParamInClks(10000, tt_timesets[ts][itCK_min]);
    timing.tXMP_LHc_min=ParamInClks(12000, tt_timesets[ts][itCK_min]);
    // CAL setup.
    timing.tCAL_min = 3750;
    timing.tCALc_min = ParamInClks(timing.tCAL_min, tt_timesets[ts][itCK_min]);
    // Boundary scan.
    timing.tBSCAN_Enable = 100000;
    timing.tBSCAN_Valid = 20000;
    // Per project settings.
    timing.tWLO_project = 6300;
endtask

task LoadTiming();
    $display("Loading timesets for '%m' @%0t", $time);
    // All timesets initialize to UNLOADED.
    for (int i=0;i<NUM_TS;i++) begin
        tt_timesets[i][TS_LOADED] = 0;
    end
    // DDR4_                               1333    1600    1866   2133    2400    2667    2934    3200          
    // UTYPE_TS                         TS_1500 TS_1250 TS_1072 TS_938  TS_833  TS_750  TS_682  TS_625          
    //          tParam                  ------- ------- ------- ------  ------  ------  ------  ------          
    SetTSArray (TS_LOADED,               1,      1,      1,      1,      1,      1,      1,      1         );
    SetTSArray_str (                     "1333", "1600", "1866", "2133", "2400", "2666", "2933", "3200"   );
    SetTSArray (itCK_min,                1500,   1250,   1071,   937,    833,    750,    682,    625       );
    SetTSArray (iCLc,                    9,      10,     12,     14,     16,     17,     19,     20       );
    SetTSArray (itRCDc,                  9,      10,     12,     14,     16,     17,     19,     20     );
    SetTSArray (itRPc,                   9,      10,     12,     14,     16,     17,     19,     20      );
    SetTSArray (itRASc,                  24,     28,     32,     36,     39,     43,     47,     52      );
    SetTSArray (itRCc,                   33,     38,     44,     50,     55,     60,     66,     72      );
    SetTSArray (itWRc,                   11,     12,     14,     16,     18,     20,     22,     24        );
    SetTSArray (itRTPc,                  5,      6,      7,      8,      9,      10,     11,     12       );
    SetTSArray (iCWLc,                   8,      9,      10,     11,     12,     14,     16,     16       );
    SetTSArray (itCCDc_S,                4,      4,      4,      4,      4,      4,      4,      4         );
    SetTSArray (itCCDc_L,                5,      5,      5,      6,      6,      7,      8,      8         );
    SetTSArray (itRRDc_S_512,            4,      4,      4,      4,      4,      4,      4,      4         );
    SetTSArray (itRRDc_S_1k,             4,      4,      4,      4,      4,      4,      4,      4         );
    SetTSArray (itRRDc_S_2k,             4,      5,      5,      6,      7,      8,      8,      9         );
    SetTSArray (itRRDc_L_512,            4,      5,      5,      6,      6,      7,      8,      8         );
    SetTSArray (itRRDc_L_1k,             4,      5,      5,      6,      6,      7,      8,      8         );
    SetTSArray (itRRDc_L_2k,             4,      6,      6,      7,      8,      9,      10,     11         );
    SetTSArray (itWTRc_S,                2,      2,      3,      3,      3,      4,      4,      4         );
    SetTSArray (itWTRc_L,                5,      6,      7,      8,      9,      10,     11,     12        );
    SetTSArray (itFAWc_512,              10,     16,     16,     16,     16,     16,     16,     16      );
    SetTSArray (itFAWc_1k,               10,     20,     22,     23,     26,     28,     31,     34      );
    SetTSArray (itFAWc_2k,               10,     28,     28,     32,     36,     40,     44,     48      );
    // SetTSArray (itRCD,                   13500,  12500,  12852,  13118,  12495,  12750,  12958,  12500     );
    // SetTSArray (itWR,                    16500,  15000,  14994,  14992,  14994,  15000,  15004,  15000      );
    // SetTSArray (itRTP,                   7500,   7500,   7497,   7496,   7497,   7500,   7502,   7500       );
    // SetTSArray (itRC,                    54000,  47500,  47124,  46850,  44982,  45000,  45012,  45000     );
    // SetTSArray (itRP,                    12000,  12500,  12852,  13118,  12495,  12750,  12958,  12500      );
    // SetTSArray (itRAS,                   36000,  35000,  34272,  33732,  32487,  32250,  32054,  32500      );
    // SetTSArray (itRRD_S_512,             6000,   5000,   4284,   3748,   3332,   3000,   2728,   2500        );
    // SetTSArray (itRRD_S_1k,              6000,   5000,   4284,   3748,   3332,   3000,   2728,   2500        );
    // SetTSArray (itRRD_S_2k,              6000,   6250,   5355,   5622,   5831,   6000,   5456,   5625        );
    // SetTSArray (itRRD_L_512,             6000,   6250,   5355,   5622,   4998,   5250,   5456,   5000        );
    // SetTSArray (itRRD_L_1k,              6000,   6250,   5355,   5622,   4998,   5250,   5456,   5000        );
    // SetTSArray (itRRD_L_2k,              6000,   7500,   6426,   6559,   6664,   6750,   6820,   6875        );
    // SetTSArray (itFAW_512,               24000,  20000,  17136,  14992,  13328,  12000,  10912,  10000      );
    // SetTSArray (itFAW_1k,                30000,  25000,  23562,  21551,  21658,  21000,  21142,  21250      );
    // SetTSArray (itFAW_2k,                42000,  35000,  29988,  29984,  29988,  30000,  30008,  30000      );

    SetTSArray (itCK_max,                1874,   1499,   1249,   1071,   937,    832,    749,    681       );
    SetTSArray (itDQSQ,                  0,      0,      0,      0,      0,      0,      0,      0         );
    SetTSArray (itDS,                    125,    125,    125,    125,    125,    125,    125,    125       );
    SetTSArray (itDH,                    125,    125,    125,    125,    125,    125,    125,    125       );
    SetTSArray (itIPW,                   750,    560,    535,    470,    416,    375,    341,    312       );
    SetTSArray (itDQSCK,                 0,      0,      0,      0,      0,      0,      0,      0         );
    SetTSArray (itDQSCK_min,             -300,   -225,   -195,   -180,   -166,   -150,   -136,   -125      );
    SetTSArray (itDQSCK_max,             300,    225,    195,    180,    166,    150,    136,    125       );
    SetTSArray (itDLLKc_min,             512,    597,    597,    768,    768,    768,    768,    768       );
    SetTSArray (itWTRc_S_CRC_DM,         4,      5,      5,      5,      5,      6,      6,      6         );
    SetTSArray (itWTRc_L_CRC_DM,         5,      5,      5,      5,      5,      5,      6,      6         );
    SetTSArray (itMOD,                   36000,  30000,  22728,  22512,  20000,  18000,  16368,  15000     );
    SetTSArray (itRP_ref,                30000,  30000,  30000,  30000,  30000,  30000,  30000,  30000     );
    SetTSArray (itIS,                    170,    170,    170,    170,    170,    170,    170,    170       );
    SetTSArray (itIH,                    120,    120,    120,    120,    120,    120,    120,    120       );
    SetTSArray (itDIPW,                  450,    360,    320,    280,    250,    230,    200,    190       );
    SetTSArray (itCKE,                   5000,   5000,   5000,   5000,   5000,   5000,   5000,   5000      );
    SetTSArray (itCPDEDc,                4,      4,      4,      4,      4,      4,      4,      4         );
    SetTSArray (itXP,                    6000,   6000,   6000,   6000,   6000,   6000,   6000,   6000      );
    SetTSArray (itXPDLL,                 24000,  24000,  24000,  24000,  24000,  24000,  24000,  24000     );
    SetTSArray (itACTPDENc,              0,      0,      0,      1,      1,      1,      1,      1         );
    SetTSArray (itPREPDENc,              0,      0,      0,      1,      1,      1,      1,      1         );
    SetTSArray (itREFPDENc,              0,      0,      0,      1,      1,      1,      1,      1         );
    SetTSArray (itZQinitc,               1024,   1024,   1024,   1024,   1024,   1024,   1024,   1024      );
    SetTSArray (itZQoperc,               512,    512,    512,    512,    512,    512,    512,    512       );
    SetTSArray (itZQCSc,                 128,    128,    128,    128,    128,    128,    128,    128       );
    SetTSArray (itWLS,                   195,    163,    140,    122,    109,    98,     89,     82        );
    SetTSArray (itWLH,                   195,    163,    140,    122,    109,    98,     89,     82        );
    SetTSArray (itAON_min,               -225,   -225,   -195,   -180,   -180,   -180,   -180,   -180      );
    SetTSArray (itAON_max,               225,    225,    195,    180,    180,    180,    180,    180       );
    SetTSArray (itPAR_ALERT_PWc,         47,     72,     84,     96,     108,    108,    108,    108       );
    SetTSArray (itPAR_ALERT_PWc_min,     40,     48,     56,     64,     72,     72,     72,     72        );
    SetTSArray (itPAR_ALERT_PWc_max,     100,    96,     112,    128,    144,    160,    176,    192       );
    for (UTYPE_TS ts=ts.first();ts<ts.last();ts=ts.next()) begin
        if (1 == tt_timesets[ts][TS_LOADED]) begin
            if (tt_timesets[ts][itCK_min] < min_tCK)
                min_tCK = tt_timesets[ts][itCK_min];
            if (tt_timesets[ts][itCK_max] > max_tCK)
                max_tCK = tt_timesets[ts][itCK_max];
            $display("\tLoaded timeset:%0s", ts.name());
        end else begin
            $display("\tTimeset:%0s was not loaded.", ts.name());
        end
    end
endtask

task SetTSArray(UTYPE_time_spec spec, int ts_1500, int ts_1250, int ts_1072,
                int ts_938, int ts_833, int ts_750, int ts_682, int ts_625);
    tt_timesets[TS_1500][spec] = ts_1500;
    tt_timesets[TS_1250][spec] = ts_1250;
    tt_timesets[TS_1072][spec] = ts_1072;
    tt_timesets[TS_938][spec] = ts_938;
    tt_timesets[TS_833][spec] = ts_833;
    tt_timesets[TS_750][spec] = ts_750;
    tt_timesets[TS_682][spec] = ts_682;
    tt_timesets[TS_625][spec] = ts_625;
endtask

task SetTSArray_str(bit[159:0] ts_1500, bit[159:0] ts_1250, bit[159:0] ts_1072,
                bit[159:0] ts_938, bit[159:0] ts_833, bit[159:0] ts_750, bit[159:0] ts_682, bit[159:0] ts_625);
    tt_timenames[TS_1500] = ts_1500;
    tt_timenames[TS_1250] = ts_1250;
    tt_timenames[TS_1072] = ts_1072;
    tt_timenames[TS_938] = ts_938;
    tt_timenames[TS_833] = ts_833;
    tt_timenames[TS_750] = ts_750;
    tt_timenames[TS_682] = ts_682;
    tt_timenames[TS_625] = ts_625;
endtask

function bit FindTimesetCeiling(int tck, output UTYPE_TS new_ts);
    bit found;

    found = 0;
    for (UTYPE_TS ts=ts.first();ts<ts.last();ts=ts.next()) begin
        if ((1 == tt_timesets[ts][TS_LOADED]) && ((tck <= tt_timesets[ts][itCK_max]) && (tck >= tt_timesets[ts][itCK_min]))) begin
            new_ts = ts;
            found = 1;
        end
    end
    return found;
endfunction

function int ParamInClks(int param_in_ps, int tCK_in_ps);
    `ifdef ALLOW_JITTER
    return ((((1000 * param_in_ps) / tCK_in_ps) + 974) / 1000);
    `endif
    if (0 == (param_in_ps % tCK_in_ps))
        return (param_in_ps / tCK_in_ps);
    else
        return ((param_in_ps / tCK_in_ps) + 1);
endfunction

function int GetTimesetValue(UTYPE_time_spec spec, UTYPE_TS ts=timing.ts_loaded);
    return tt_timesets[ts][spec];
endfunction
