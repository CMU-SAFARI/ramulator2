import os, yaml, copy, itertools
from calc_rh_parameters import get_rh_parameters

base_config_file = "rh_baseline.yaml"
base_config = None
with open(base_config_file, 'r') as stream:
    try:
        base_config = yaml.safe_load(stream)
    except yaml.YamlError as exc:
        print(exc)
if(base_config == None):
    print("Error: base config is None")
    exit(1)

output_path = "./results_singlecore"
trace_path = "./cputraces"
trace_combination_filename = "multicore_traces.txt"

trace_names = []
with open(trace_combination_filename, "r") as trace_combination_file:
    for line in trace_combination_file:
        line = line.strip()
        if(line == ""):
            continue
        trace_list = line.split(",")[1:]
        trace_names += trace_list

trace_names = list(set(trace_names))

for trace_name in trace_names:
    for mitigation in ["PARA", "Hydra", "TWiCe-Ideal", "Graphene", "OracleRH", "RRS", "NoDefence"]:
        for tRH in [2000, 1000, 500, 200, 100]:
            for path in [output_path + "/" + mitigation + "/stats", output_path + "/" + mitigation + "/configs", output_path + "/" + mitigation + "/cmd_count", output_path + "/" + mitigation + "/dram_trace"]:
                if not os.path.exists(path):
                    os.makedirs(path)

            result_filename = output_path + "/" + mitigation + "/stats/" + str(tRH) + "_" + trace_name + ".txt"
            config_filename = output_path + "/" + mitigation + "/configs/" + str(tRH) + "_" + trace_name + ".yaml"
            cmd_count_filename = output_path + "/" + mitigation + "/cmd_count/" + str(tRH) + "_" + trace_name + ".cmd.count"
            dram_trace_filename = output_path + "/" + mitigation + "/dram_trace/" + str(tRH) + "_" + trace_name + ".dram.trace"
            config = copy.deepcopy(base_config)
            result_file = open(result_filename, "w")
            config_file = open(config_filename, "w")
            
            config['Frontend']['traces'] = [trace_path + "/" + trace_name]
            config['MemorySystem']['Controller']['plugins'][0]['ControllerPlugin']['path'] = cmd_count_filename
            # config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'TraceRecorder', 'path': dram_trace_filename}})
            if(mitigation == "PARA"):
                threshold = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'PARA', 'threshold': threshold}})
            elif(mitigation == "TWiCe-Ideal"):
                twice_rh_threshold, twice_pruning_interval_threshold = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'TWiCe-Ideal', 'twice_rh_threshold': twice_rh_threshold, 'twice_pruning_interval_threshold': twice_pruning_interval_threshold}})
            elif(mitigation == "Graphene"):
                num_table_entries, activation_threshold, reset_period_ns = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'Graphene', 'num_table_entries': num_table_entries, 'activation_threshold': activation_threshold, 'reset_period_ns': reset_period_ns}})
            elif(mitigation == "OracleRH"):
                tRH = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'OracleRH', 'tRH': tRH}})
            elif(mitigation == "Hydra"):
                hydra_tracking_threshold, hydra_group_threshold, hydra_row_group_size, hydra_reset_period_ns, hydra_rcc_num_per_rank, hydra_rcc_policy = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'Hydra', 'hydra_tracking_threshold': hydra_tracking_threshold, 'hydra_group_threshold': hydra_group_threshold, 'hydra_row_group_size': hydra_row_group_size, 'hydra_reset_period_ns': hydra_reset_period_ns, 'hydra_rcc_num_per_rank': hydra_rcc_num_per_rank, 'hydra_rcc_policy': hydra_rcc_policy}})
            elif(mitigation == "RRS"):
                num_hrt_entries, num_rit_entries, rss_threshold, reset_period_ns = get_rh_parameters(mitigation, tRH)
                config['MemorySystem']['Controller']['plugins'].append({'ControllerPlugin' : {'impl': 'RRS', 'num_hrt_entries': num_hrt_entries, 'num_rit_entries': num_rit_entries, 'rss_threshold': rss_threshold, 'reset_period_ns': reset_period_ns}})
            elif(mitigation == "NoDefense"):
                pass
            cmd = "srun ./ramulator -c '" + str(config) + "' > " + result_filename + " 2>&1 &"           
            
            yaml.dump(config, config_file, default_flow_style=False)
            config_file.close()
            result_file.write(cmd + "\n")
            result_file.close()
            
            print("Running: trace = " + trace_name + ", mitigation = " + mitigation + ", tRH = " + str(tRH))
            os.system(cmd)
