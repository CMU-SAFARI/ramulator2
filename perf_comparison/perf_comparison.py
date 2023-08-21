import os
import sys
import subprocess
import time
import yaml
import pandas as pd


def time_execution(args):
  print(f"Running {args}...")
  start_time = time.time()
  r = subprocess.run(args, capture_output = True, text = True)
  end_time = time.time()
  
  elapsed = end_time - start_time
  return elapsed, r


def main():
  simulators = ["ramulatorv1", "ramulatorv2", "dramsim2", "dramsim3", "usimm"]
  traces = ["stream", "random"]
  num_itrs = 10

  columns = ["simulator", "trace", "elapsed_time", "itr"]
  results = []
  for itr in range(num_itrs):
    for trace in traces:
      for simulator in simulators:

        # Ramulator V1
        if simulator == "ramulatorv1":
          args = [
            "./ramulatorv1",
            "./configs/ramulatorv1.cfg",
            "--mode=dram", 
            "--stats", 
            f"./output/ramulatorv1/{trace}.stats", 
            f"./traces/{trace}_5M_R8W2_ramulatorv1.trace"
          ]
          time, r = time_execution(args)
          results.append(["ramulatorv1", trace, time, itr])

        # Ramulator V2
        elif simulator == "ramulatorv2":
          config_file = "./configs/ramulatorv2.yaml"
          config = None
          with open(config_file) as f:
            config = yaml.load(f, Loader=yaml.FullLoader)
            config["Frontend"]["path"] = f"./traces/{trace}_5M_R8W2_ramulatorv2.trace"

          args = [
            "./ramulatorv2",
            "--config", 
            yaml.dump(config) 
          ]
          time, r = time_execution(args)
          with open(f"./output/ramulatorv2/{trace}.stdout", "w") as f:
            f.write(r.stdout)
          results.append(["ramulatorv2", trace, time, itr])

        # dramsim2
        elif simulator == "dramsim2":
          args = [
            "./dramsim2",
            "-s", "./configs/dramsim2_system.ini", 
            "-d", "./configs/dramsim2_dram.ini", 
            "-c", "5000000000",
            "-t", f"./traces/mase_{trace}_5M_R8W2_dramsim2.trace"
          ]
          time, r = time_execution(args)
          with open(f"./output/dramsim2/{trace}.stdout", "w") as f:
            f.write(r.stdout)
          results.append(["dramsim2", trace, time, itr])

        # dramsim3
        elif simulator == "dramsim3":
          args = [
            "./dramsim3",
            "./configs/dramsim3.ini", 
            "-t", f"./traces/{trace}_5M_R8W2_dramsim3.trace",
            "-c", "5000000000",
            "-o", "./output/dramsim3"
          ]
          time, r = time_execution(args)
          os.rename(f"./output/dramsim3/dramsim3.txt", f"./output/dramsim3/{trace}.stats")
          results.append(["dramsim3", trace, time, itr])

        # usimm
        elif simulator == "usimm":
          args = [
            "./usimm",
            "./configs/usimm.cfg", 
            f"./traces/{trace}_5M_R8W2_usimm.trace",
          ]
          time, r = time_execution(args)
          with open(f"./output/usimm/{trace}.stdout", "w") as f:
            f.write(r.stdout)
          results.append(["usimm", trace, time, itr])

  df = pd.DataFrame(results)
  df.columns = columns
  df.to_csv("results.csv")


if __name__ == "__main__":
  main()
