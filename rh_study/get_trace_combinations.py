import random

num_samples_per_group = 5

output_filename = "multicore_traces.txt"
output_file = open(output_filename, "w")

group_list = ["HHHH", "HHHL", "HHLL", "HLLL", "LLLL"]

high_traces = ["471.omnetpp","505.mcf","482.sphinx3","483.xalancbmk","450.soplex","437.leslie3d","433.milc","510.parest","434.zeusmp","519.lbm","459.GemsFDTD","549.fotonik3d","462.libquantum","470.lbm","429.mcf"]
low_traces = ["511.povray","541.leela","481.wrf","538.imagick","447.dealII","464.h264ref","444.namd","456.hmmer","403.gcc","526.blender","544.nab","531.deepsjeng","525.x264","445.gobmk","458.sjeng","508.namd","401.bzip2","435.gromacs","502.gcc","523.xalancbmk","500.perlbench","557.xz","507.cactuBSSN","436.cactusADM","473.astar","520.omnetpp"]

for group in group_list:
    num_h = group.count("H")
    num_l = group.count("L")

    for i in range(num_samples_per_group):
        highs = random.sample(high_traces, num_h)
        lows = random.sample(low_traces, num_l)

        traces = highs + lows
        output_file.write(group + str(i) + ",")
        output_file.write(",".join(traces) + "\n")
