import subprocess
import os
import yaml
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

#local paths
binary_path = "../ramulator2"

base_config_files = [
    "./configs/ddr5_4800an_16ch.yaml",
]

base_output_dirs = [config_file.split('/')[-1].replace("config", "results").replace(".yaml", "") for config_file in base_config_files]

nops_list = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17, 20, 25, 30, 40, 50, 75, 100, 200, 300, 1000, 10000]
read_ratios = [1, 0.9, 0.8, 0.7, 0.6, 0.5]

# Number of concurrent threads
MAX_WORKERS = 16 

def get_ratio_label(ratio):
    if ratio == 1:
        return "read"
    elif ratio == 0.5:
        return "read_write"
    elif ratio == 0:
        return "write"
    else:
        return f"custom_{int(ratio * 100)}"

def run_single_test(nop, read_ratio, base_config_file, base_output_dir):
    """Run a single test configuration"""
    with open(base_config_file, 'r') as f:
        config = yaml.safe_load(f)
    
    config['Frontend']['ratio_reads'] = float(read_ratio)
    config['Frontend']['nop_counter'] = nop 

    ratio_label = get_ratio_label(read_ratio)
    sub_dir = os.path.join(base_output_dir, ratio_label)
    os.makedirs(sub_dir, exist_ok=True)

    suffix = f"_{nop}"
    modified_config_path = os.path.join(sub_dir, f"config{suffix}.yaml")
    output_path = os.path.join(sub_dir, f"output{suffix}.txt")
    error_path = os.path.join(sub_dir, f"error{suffix}.txt")

    with open(modified_config_path, 'w') as f:
        yaml.dump(config, f)

    with open(output_path, 'w') as out, open(error_path, 'w') as err:
        process = subprocess.run(
            [binary_path, "-f", modified_config_path],
            stdout=out,
            stderr=err
        )
    
    with print_lock:
        print(f"Completed {output_path}, with config: {modified_config_path}")
        print(f"Process exited with code {process.returncode}")
    
    return (nop, read_ratio, process.returncode)

print_lock = threading.Lock()

test_configs = [(nop, read_ratio, base_config_file, base_output_dir) for nop in nops_list for read_ratio in read_ratios for base_config_file, base_output_dir in zip(base_config_files, base_output_dirs)]

print(f"Running {len(test_configs)} tests with {MAX_WORKERS} concurrent threads...")

with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
    future_to_config = {
        executor.submit(run_single_test, nop, read_ratio, base_config_file, base_output_dir): (nop, read_ratio, base_config_file, base_output_dir)
        for nop, read_ratio, base_config_file, base_output_dir in test_configs
    }
    
    completed = 0
    for future in as_completed(future_to_config):
        nop, read_ratio, _, _ = future_to_config[future]
        try:
            result = future.result()
            completed += 1
            with print_lock:
                print(f"Progress: {completed}/{len(test_configs)} tests completed")
        except Exception as exc:
            with print_lock:
                print(f"Test {nop}, {read_ratio} generated an exception: {exc}")

print("All tests completed!")