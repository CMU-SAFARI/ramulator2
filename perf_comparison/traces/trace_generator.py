import os
import sys
import argparse
import random


# Some constants
CL_SIZE = 64  # Cache line size in bytes
RANDOM_SEED = 0


def parse_arg():
  parser = argparse.ArgumentParser(
    description="Generate memory traces for DRAM simulators."
  )

  parser.add_argument(
    "--pattern", "-p", type=str, dest="access_pattern",
    choices=["stream", "random"],
    help="The memory access pattern of the generated trace."
  )

  parser.add_argument(
    "--num_reqs", "-n", type=int, dest="num_reqs",
    help="The number of memory requests in the trace."
  )

  parser.add_argument(
    "--ratio", "-r", type=float, dest="rw_ratio",
    default=0.8,
    help="The ratio of the number of memory read vs write requests."
  )

  parser.add_argument(
    "--type", "-t", type=str, dest="simulator_type",
    choices=["ramulatorv1", "ramulatorv2", "dramsim2", "dramsim3", "usimm"],
    help="The DRAM simulator that the trace will be used for."
  )

  parser.add_argument(
    "--output", "-o", type=str, dest="output_file",
    help="Output trace file path."
  )

  if len(sys.argv)==1:
    parser.print_help(sys.stderr)
    exit(-1)

  return parser.parse_args()


def gen_inst(simulator_type, clk, req_type, addr):
  addr = addr % 1073741824
  if   simulator_type == "ramulatorv1":
    if req_type == "read":
      return f"{hex(addr)} R\n"
    else:
      return f"{hex(addr)} W\n"
    
  elif simulator_type == "ramulatorv2":
    if req_type == "read":
      return f"LD {hex(addr)}\n"
    else:
      return f"ST {hex(addr)}\n"
    
  elif simulator_type == "dramsim2":
    if req_type == "read":
      return f"{hex(addr)} READ {clk}\n"
    else:
      return f"{hex(addr)} WRITE {clk}\n"
    
  elif simulator_type == "dramsim3":
    if req_type == "read":
      return f"{hex(addr)} READ {clk}\n"
    else:
      return f"{hex(addr)} WRITE {clk}\n"
    
  elif simulator_type == "usimm":
    if req_type == "read":
      return f"0 R {hex(addr)} 0x0\n"
    else:
      return f"0 W {hex(addr)}\n"
    

def main():
  args = parse_arg()
  random.seed(RANDOM_SEED)


  with open(args.output_file, "w") as f:
    addr = 0
    generated_reqs = 0
    while generated_reqs < args.num_reqs:
      if args.access_pattern == "random":
        addr = random.randrange(0, 32*1024*1024) * CL_SIZE
      else:
        addr += CL_SIZE

      r = random.uniform(0.0, 1.0)
      if r >= args.rw_ratio:
        f.write(gen_inst(args.simulator_type, generated_reqs, "write", addr))
      else:
        f.write(gen_inst(args.simulator_type, generated_reqs, "read", addr))
      generated_reqs += 1


if __name__ == "__main__":
  main()