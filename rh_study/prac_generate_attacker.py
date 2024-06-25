LLC_BYTES_PER_CORE = 2 * 1024 * 1024 # 2 MiB
NUM_CORES = 4
LLC_ASSOC = 8
CACHELINE_BYTES = 64
BUBBLE_COUNT = 0
ROW_BYTES = 1024
ROBARACOCH_ROW_OFF = 18
ROBARACOCH_BANK_OFF = 13
ROBARACOCH_RANK_OFF = 12

TOTAL_BANKS = 32

N_RANKS = 2
N_BANKGROUPS = 8
N_BANKSPERGROUP = 4
SEEDS = []
BANK_PATTERN = [i*N_BANKGROUPS + (i % N_BANKSPERGROUP) for i in range(N_BANKSPERGROUP)]

ROW_PATTERN = [64*i for i in range(9)]
for row_id in ROW_PATTERN:
    for bank_id in BANK_PATTERN:
        for rank_id in range(N_RANKS):
            SEEDS.append((row_id, bank_id, rank_id))

for i in range(len(SEEDS)):
    row, bank, rank = SEEDS[i]
    SEEDS[i] = row << ROBARACOCH_ROW_OFF
    SEEDS[i] += bank << ROBARACOCH_BANK_OFF
    SEEDS[i] += rank << ROBARACOCH_RANK_OFF

n_reads = ROW_BYTES // CACHELINE_BYTES 
for idx_read in range(n_reads):
    for seed in SEEDS:
        print(f"{BUBBLE_COUNT} {int(seed + idx_read * CACHELINE_BYTES)}")