import os, sys

class Error:
    cmd = ""
    message = ""
    timing_params = []
    
def filter_log_file(log_filename):
    log_file = open(log_filename, "r")
    logs = log_file.readlines()
    log_file.close()
    log_file = open(log_filename, "w")
    
    trace_started = False
    config_started = False
    num_errors = 0
    last_cmd = ""
    
    for i in range(len(logs)):
        if("Configurations" in logs[i]):
            config_started = True
        if("Trace Starts" in logs[i]):
            config_started = False
            trace_started = True
            log_file.write("Errors:\n")
            
        if(config_started):
            log_file.write(logs[i])
                
        if("test_done" in logs[i]):
            break
        
        if(trace_started):
            if("Reading unwritten address" in logs[i]):
                continue
            if("Cycle" in logs[i]):
                last_cmd = logs[i]
            
            if("WARNING" in logs[i] or "VIOLATION" in logs[i] or "ERROR" in logs[i]):
                error = Error()
                error.cmd = last_cmd.strip()
                error.message = logs[i].partition(":")[2].strip()
                error.timing_params = []
                for j in range(i+1, len(logs)):
                    if("Cycle" in logs[j]):
                        last_cmd = logs[j]
                        break
                    if("test_done" in logs[j]):
                        break
                    if("Reading unwritten address" in logs[j]):
                        continue
                    if("toggle around write burst" in logs[j]):
                        continue
                    error.timing_params.append(logs[j][1:].strip())

                if(len(error.timing_params) == 1 and "tRFC_dlr" in error.timing_params[0]):
                    continue
                if(len(error.timing_params) == 0):
                    continue
                log_file.write(error.cmd + "\n")
                log_file.write(error.message + "\n")
                for timing_param in error.timing_params:
                    log_file.write(timing_param + "\n")
                log_file.write("\n")

                i = j
                num_errors += 1
    
    log_file.write("Total number of errors: " + str(num_errors) + "\n")
    log_file.close()


if(len(sys.argv) != 3):
    print("Usage: python3 trace_verifier.py <trace_filepath> <output_filepath>")
    exit(1)

trace_filepath = sys.argv[1]
output_filepath = sys.argv[2]

pwd = os.getcwd()
log_filename = output_filepath + ".log"

print("Verifying trace: " + trace_filepath)

os.system("vsim -do " + pwd + "/modelsim.do -batch > " + log_filename)
os.system("cp " + log_filename + " " + log_filename + ".unfiltered")
filter_log_file(log_filename)
print("Filtered log file: " + log_filename)