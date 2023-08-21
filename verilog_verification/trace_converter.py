import os, sys

class Error:
    cmd = ""
    message = ""
    timing_params = []

def get_tb_cmd(bubble_cycle, cmd, ra, bg, ba, ro, co):
    if(bubble_cycle > 1):
        tb_cmd = f"\tdeselect({bubble_cycle-1});\n"
    else:
        tb_cmd = ""
    tb_cmd += "\t"
    if(cmd == "ACT"):
        tb_cmd += f"activate(.rank({ra}), .bg({bg}), .ba({ba}), .row({ro}));\n"
    elif(cmd == "PRE"):
        tb_cmd += f"precharge(.rank({ra}), .bg({bg}), .ba({ba}), .ap(0));\n"
    elif(cmd == "PREA"):
        tb_cmd += f"precharge(.rank({ra}), .bg({bg}), .ba({ba}), .ap(1));\n"
    elif(cmd == "RD"):
        tb_cmd += f"read(.rank({ra}), .bg({bg}), .ba({ba}), .col({co}), .ap(0));\n"
    elif(cmd == "RDA"):
        tb_cmd += f"read(.rank({ra}), .bg({bg}), .ba({ba}), .col({co}), .ap(1));\n"
    elif(cmd == "WR"):
        tb_cmd += f"write(.rank({ra}), .bg({bg}), .ba({ba}), .col({co}), .ap(0));\n"
    elif(cmd == "WRA"):
        tb_cmd += f"write(.rank({ra}), .bg({bg}), .ba({ba}), .col({co}), .ap(1));\n"
    elif(cmd == "REF1X" or cmd == "REF2X" or cmd == "REF4X" or cmd == "REFab"):
        tb_cmd += f"refresh(.rank({ra}), .bg({bg}), .ba({ba}));\n"
    else:
        print("Unknown command: " + cmd)
        exit(1)
    return tb_cmd

def convert_trace_to_tb(trace_filename, out_tb_filename):
    trace_file = open(trace_filename, "r")
    out_tb_file = open(out_tb_filename, "w")

    last_cycle = 0
    inst_num = 0
    for line in trace_file:
        line = line.strip()
        if(line == ""):
            continue
        line = line.split(",")
        current_cycle = int(line[0])
        cmd = line[1].strip()
        ra = int(line[3])
        bg = int(line[4])
        ba = int(line[5])
        ro = int(line[6])
        co = int(line[7])
        if(current_cycle <= last_cycle):
            print("Error: Traces are not incremental.")
            exit(1)
        tb_cmd = get_tb_cmd(current_cycle - last_cycle, cmd, ra, bg, ba, ro, co)
        out_tb_file.write(tb_cmd)
        last_cycle = current_cycle
        inst_num += 1
        if(inst_num > 300000):
            break
    trace_file.close()
    out_tb_file.close()
    
def configure_dram(dram_org, rank, time_spec, out_config_filename):
    config_file = open(out_config_filename, "w")
    config_file.write("`define " + dram_org + "\n")
    config_file.write("`define " + time_spec + "\n")
    if(rank == "2"):
        config_file.write("`define DUAL_RANK\n")
    config_file.close()

def check_input(dram_org, rank, time_spec):
    if(dram_org != "DDR4_2G_X4" and dram_org != "DDR4_2G_X8" and dram_org != "DDR4_2G_X16" and
        dram_org != "DDR4_4G_X4" and dram_org != "DDR4_4G_X8" and dram_org != "DDR4_4G_X16" and
        dram_org != "DDR4_8G_X4" and dram_org != "DDR4_8G_X8" and dram_org != "DDR4_8G_X16" and
        dram_org != "DDR4_16G_X4" and dram_org != "DDR4_16G_X8" and dram_org != "DDR4_16G_X16"):
        print("Error: Invalid DRAM organization.")
        exit(1)
    if(rank != "1" and rank != "2"):
        print("Error: Invalid rank.")
        exit(1)
    if(time_spec != "DDR4_1600" and time_spec != "DDR4_1866" and time_spec != "DDR4_2133" and
        time_spec != "DDR4_2400" and time_spec != "DDR4_2666" and time_spec != "DDR4_2933" and
        time_spec != "DDR4_3200"):
        print("Error: Invalid time spec.")
        exit(1)




if(len(sys.argv) != 5):
    print("Usage: python3 trace_verifier.py <DRAM Org.> <Rank> <Time Spec.> <trace_filepath>")
    exit(1)

dram_org = sys.argv[1]
rank = sys.argv[2]
time_spec = sys.argv[3]
trace_filepath = sys.argv[4]

check_input(dram_org, rank, time_spec)

pwd = os.getcwd()
out_tb_filename = pwd + "/sources/trace_tb.v"
out_config_filename = pwd + "/sources/trace_config.vh"

print("Converting trace: " + trace_filepath)
configure_dram(dram_org, rank, time_spec, out_config_filename)
convert_trace_to_tb(trace_filepath, out_tb_filename)
