Disclaimer of Warranty:
-----------------------
This software code and all associated documentation, comments or other 
information (collectively "Software") is provided "AS IS" without 
warranty of any kind. MICRON TECHNOLOGY, INC. ("MTI") EXPRESSLY 
DISCLAIMS ALL WARRANTIES EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
TO, NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND ANY IMPLIED WARRANTIES 
OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE. MTI DOES NOT 
WARRANT THAT THE SOFTWARE WILL MEET YOUR REQUIREMENTS, OR THAT THE 
OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE. 
FURTHERMORE, MTI DOES NOT MAKE ANY REPRESENTATIONS REGARDING THE USE OR 
THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS, 
ACCURACY, RELIABILITY, OR OTHERWISE. THE ENTIRE RISK ARISING OUT OF USE 
OR PERFORMANCE OF THE SOFTWARE REMAINS WITH YOU. IN NO EVENT SHALL MTI, 
ITS AFFILIATED COMPANIES OR THEIR SUPPLIERS BE LIABLE FOR ANY DIRECT, 
INDIRECT, CONSEQUENTIAL, INCIDENTAL, OR SPECIAL DAMAGES (INCLUDING, 
WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION, 
OR LOSS OF INFORMATION) ARISING OUT OF YOUR USE OF OR INABILITY TO USE 
THE SOFTWARE, EVEN IF MTI HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH 
DAMAGES. Because some jurisdictions prohibit the exclusion or 
limitation of liability for consequential or incidental damages, the 
above limitation may not apply to you.

Copyright 2011 Micron Technology, Inc. All rights reserved.
MICRON TECHNOLOGY, INC. - CONFIDENTIAL AND PROPRIETARY INFORMATION

Getting Started:
----------------
Unzip the included files to a folder.
Compile and run using 'run' scripts listed below.

File Descriptions:
------------------
readme.txt          // this file

----Project files----
arch_package.sv     // Defines parameters, enums and structures for DDR4.
arch_defines.v      // Defines chip sizes and widths.
ddr4_model.svp       // Defines ideal DDR4 dram behavior.
interface.sv        // Defines 'interface iDDR4'.
MemoryArray.svp      // Defines 'class MemoryArray'.
proj_package.sv     // Defines parameters, enums and structures for this
                    // specific DDR4.
StateTable.svp       // Wrapper around StateTableCore which creates 
                    // 'module StateTable'.
StateTableCore.svp   // Dram state core functionality.
timing_tasks.sv     // Defines enums and timing parameters for 
                    // available speed grades.

----Testbench----
tb.sv               // ddr4 model test bench.
subtest.vh          // Example test included by the test bench.

----Compile and run scripts----
run_modelsim        // Compiles and runs for modelsim (uses modelsim.do).
run_ncverilog       // Compiles and runs for ncverilog.
run_vcs             // Compiles and runs for vcs.
modelsim.do         // For use with modelsim.

---- Other Compilers ----
Aldec compiler is available here: https://www.aldec.com/files/file/ddr4_verilog_models_aldec.zip

Defining the Organization:
--------------------------
The verilog compiler directive "`define" may be used to choose between 
multiple organizations supported by the ddr4 model.
Valid organizations include: "X4", "X8", and "X16".
Valid sizes include: "2G", "4G", "8G", and "16G".
These two parameters are combined to define the organization tested. 
For example DDR4_4G_X8. Please see arch_defines.v for examples.
The following are examples of defining the organization.

    simulator   command line
    ---------   ------------
    ModelSim    vlog +define+DDR4_2G_X8 ddr4_model.svp [additional files]
    NC-Verilog  ncverilog +define+DDR4_2G_X8 ddr4_model.svp [additional files]
    VCS         vcs +define+DDR4_2G_X8 ddr4_model.svp [additional files]

All combinations of size and organization are considered valid 
by the ddr4 model even though a Micron part may not exist for every 
combination.

Use these +define parameters for debugging:
MODEL_DEBUG_MEMORY  // Prints messages for every read and write to the memory core.
MODEL_DEBUG_CMDS    // Prints messages for every command/clk on the dram bus.
ALLOW_JITTER        // If clk stays within 2%, then do not change timing parameters.
ENABLE_MODEL_VCD_DUMP  // dump vcd file to micron_model_dump.vcd

Use these function calls on the model for customizing memory array behavior
function void set_unwritten_memory_default(logic unwritten_memory_default = 1'bx, bit unwritten_random = 0);
function void set_memory_warnings(bit print_warnings, bit debug);

Use one of these +define parameters to fix the model to a specific speed grade:
FIXED_1333
FIXED_1600
FIXED_1866
FIXED_2133
FIXED_2400
FIXED_2666
FIXED_2933
FIXED_3200

Please see tb.sv for an example of builing a testbench and controlling the DDR4 interface.
The file subtest.vh uses tb.v to show some simple read and write patterns. 
Note: Normally the model adjusts in the input tck for timing parameters. This can be fixed
by using set_timing_parameter_lock(). Please see subtest.vh for an example.

Multiple model configurations can be selected at simulation time.
    // Component instantiation
    ddr4_model #(.CONFIGURED_DQ_BITS(8), .CONFIGURED_DENSITY(_2G), .CONFIGURED_RANKS(1)) 
               model_2Gx8(.model_enable(model_enable), .iDDR4(iDDR4));
    // Component instantiation
    ddr4_model #(.CONFIGURED_DQ_BITS(16), .CONFIGURED_DENSITY(_4G), .CONFIGURED_RANKS(1)) 
               model_4Gx16(.model_enable(model_enable), .iDDR4(iDDR4));
