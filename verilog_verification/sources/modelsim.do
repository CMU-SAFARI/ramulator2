#########################################################################################
#
#   Disclaimer   This software code and all associated documentation, comments or other 
#  of Warranty:  information (collectively "Software") is provided "AS IS" without 
#                warranty of any kind. MICRON TECHNOLOGY, INC. ("MTI") EXPRESSLY 
#                DISCLAIMS ALL WARRANTIES EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
#                TO, NONINFRINGEMENT OF THIRD PARTY RIGHTS, AND ANY IMPLIED WARRANTIES 
#                OF MERCHANTABILITY OR FITNESS FOR ANY PARTICULAR PURPOSE. MTI DOES NOT 
#                WARRANT THAT THE SOFTWARE WILL MEET YOUR REQUIREMENTS, OR THAT THE 
#                OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE. 
#                FURTHERMORE, MTI DOES NOT MAKE ANY REPRESENTATIONS REGARDING THE USE OR 
#                THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS, 
#                ACCURACY, RELIABILITY, OR OTHERWISE. THE ENTIRE RISK ARISING OUT OF USE 
#                OR PERFORMANCE OF THE SOFTWARE REMAINS WITH YOU. IN NO EVENT SHALL MTI, 
#                ITS AFFILIATED COMPANIES OR THEIR SUPPLIERS BE LIABLE FOR ANY DIRECT, 
#                INDIRECT, CONSEQUENTIAL, INCIDENTAL, OR SPECIAL DAMAGES (INCLUDING, 
#                WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION, 
#                OR LOSS OF INFORMATION) ARISING OUT OF YOUR USE OF OR INABILITY TO USE 
#                THE SOFTWARE, EVEN IF MTI HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH 
#                DAMAGES. Because some jurisdictions prohibit the exclusion or 
#                limitation of liability for consequential or incidental damages, the 
#                above limitation may not apply to you.
#
#                Copyright 2003 Micron Technology, Inc. All rights reserved.
#
#########################################################################################

vlib work
vlog -work work +acc -l vcs.log -sv arch_package.sv proj_package.sv interface.sv StateTable.svp MemoryArray.svp ddr4_model.svp tb.sv
vsim tb
run -all
