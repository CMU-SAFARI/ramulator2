import os
import sys
import argparse
import random

def parse_args():
  parser = argparse.ArgumentParser(
    description="Synthetic trace generator for Ramulator 2.0"
  )

  parser.add_argument(
    "--type", "-t", type=str, dest="trace_type",
    choices=['SimpleO3', 'LStrace'],
    default="SimpleO3",
    help="Frontend type (default: Simple O3)"
  )

  parser.add_argument(
    "--pattern", "-p", type=str, dest="access_pattern",
    choices=['stream', 'random'],
    help="The memory access pattern (choices: stream, random)"
  )

  parser.add_argument(
    "--num-insts", "-n", type=int, dest="num_insts",
    default=100000000,
    help="The number of instructions in the trace"
  )

  parser.add_argument(
    "--output", "-o", type=str, dest="out_file",
    required=True,
    help="The output filename"
  )

#  SimpleO3 parameters
  parser.add_argument(
    "--distance", "-d", type=int, dest="req_dist",
    default=10,
    help="The number of non-memory instructions between consecutive memory requests"
  )

#  LStrace parameters
  parser.add_argument(
    "--ls_ratio", "-r", type=float, dest="load_store_ratio",
    default=0.8,
    help="The ratio between load ans store instructions"
  )

  args = parser.parse_args()
  return args


def gen_SimpleO3_trace(args):
  # constants
  CACHE_LINE_SIZE = 64 # the main memory access granularity
  RANDOM_SEED = 0
  random.seed(a=RANDOM_SEED)
  trace_file = open(args.out_file, "w")

  generated_insts = 0
  # variables used depending on the access pattern
  stream_addr = 0

  while generated_insts < args.num_insts:
    cur_line=str(args.req_dist) + " "
    if args.access_pattern == 'stream':
      cur_line = cur_line + str(stream_addr)
      stream_addr += CACHE_LINE_SIZE
    elif args.access_pattern == 'random':
      rand_addr = random.getrandbits(30) 
      cur_line = cur_line + str(rand_addr)
    else:
      print ("Error: Unimplemented access pattern: ", args.access_pattern, "!")
      sys.exit(-2)

    trace_file.write(cur_line + '\n')
    generated_insts += args.req_dist
  trace_file.close()


def gen_LStrace(args):
  if args.load_store_ratio < 0.0 or args.load_store_ratio > 1.0:
    print("Invalid load store ratio.")
    sys.exit(-2)

  # constants
  CACHE_LINE_SIZE = 64 # the main memory access granularity
  RANDOM_SEED = 0
  random.seed(a=RANDOM_SEED)
  trace_file = open(args.out_file, "w")

  generated_insts = 0
  # variables used depending on the access pattern
  stream_addr = 0

  while generated_insts < args.num_insts:
    req_type = "LD"
    ls_sample = random.uniform(0.0, 1.0)
    if ls_sample > args.load_store_ratio:
      req_type = "ST"

    cur_line = req_type + " "
    if args.access_pattern == 'stream':
        cur_line = cur_line + str(stream_addr)
        stream_addr += CACHE_LINE_SIZE
    elif args.access_pattern == 'random':
        rand_addr = random.getrandbits(30) 
        cur_line = cur_line + str(rand_addr)
    else:
        print ("Error: Unimplemented access pattern: ", args.access_pattern, "!")
        sys.exit(-2)

    trace_file.write(cur_line + '\n')
    generated_insts += 1
  trace_file.close()


def main():
  args = parse_args()
  if(os.path.isfile(args.out_file)):
    print ("Error: Cannot write the output.")
    print ("The output file '" + args.out_file + "' already exists.")
    sys.exit(-1)

  if args.trace_type == "SimpleO3":
    gen_SimpleO3_trace(args)
  elif args.trace_type == "LStrace":
    gen_LStrace(args)
  else:
    print("Unrecognized trace type")
    exit(-2)

if __name__ == "__main__":
  main()