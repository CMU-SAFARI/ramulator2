# %%
import os
import re
import matplotlib.pyplot as plt
import matplotlib as mpl
import pandas as pd
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

bytes_per_RW = 64
tCK_ps = 416  # DDR5 clock period in picoseconds
mem_clk_freq = 1 / (tCK_ps * 1e-12)  # Hz

max_achieved_bandwidth = 0

num_channels = 16

result_dir = f'ddr5_4800an_{num_channels}ch'

input_dirs = [
    ("100% reads", f"./{result_dir}/read"),
    ("50% reads", f"./{result_dir}/read_write"),
    ("60% reads", f"./{result_dir}/custom_60"),
    ("70% reads", f"./{result_dir}/custom_70"),
    ("80% reads", f"./{result_dir}/custom_80"),
    ("90% reads", f"./{result_dir}/custom_90")
]

# colorbar labels
label_to_reads = {
    "100% reads": 100,
    "95% reads": 95,
    "90% reads": 90,
    "85% reads": 85,
    "80% reads": 80,
    "75% reads": 75,
    "70% reads": 70,
    "65% reads": 65,
    "60% reads": 60,
    "55% reads": 55,
    "50% reads": 50,
}

all_dfs = []

for label, output_dir in input_dirs:
    data = []

    for filename in os.listdir(output_dir):
        if filename.startswith('output_') and filename.endswith('.txt'):
            match = re.match(r'output_(\d+)\.txt', filename)
            if match:
                nop_count = int(match.group(1))
                filepath = os.path.join(output_dir, filename)

                memory_cycles = None
                avg_latency_ns = None

                total_number_of_writes = None
                total_number_of_reads = None

                with open(filepath, 'r') as file:
                    for line in file:
                        if 'memory_system_cycles' in line:
                            cycle_match = re.search(r'memory_system_cycles:\s*(\d+)', line)
                            if cycle_match:
                                memory_cycles = int(cycle_match.group(1))
                        if 'total_num_write_requests' in line:
                            write_match = re.search(r'total_num_write_requests:\s*(\d+)', line)
                            if write_match:
                                total_number_of_writes = float(write_match.group(1))
                        if 'total_num_read_requests' in line:
                            read_match = re.search(r'total_num_read_requests:\s*(\d+)', line)
                            if read_match:
                                total_number_of_reads = float(read_match.group(1)) 
                        if 'avg_read_latency_0' in line:
                            lat_match = re.search(r'avg_read_latency_0:\s*([\d.]+)', line)
                            if lat_match:
                                avg_latency_ns = float(lat_match.group(1))*tCK_ps/1000
                        if 'avg_queue_time_stream0' in line:
                            lat_match = re.search(r'avg_queue_time_stream0:\s*([\d.]+)', line)
                            if lat_match:
                                avg_stream_queue = float(lat_match.group(1))/(1000/tCK_ps)
                        if 'avg_random_read_latency_0' in line:
                            lat_match = re.search(r'avg_random_read_latency_0:\s*([\d.]+)', line)
                            if lat_match:
                                avg_random_read_latency = float(lat_match.group(1))/(1000/tCK_ps)
                        if 'average_strided_read_latency_0' in line:
                            lat_match = re.search(r'average_strided_read_latency_0:\s*([\d.]+)', line)
                            if lat_match:
                                avg_strided_latency_ns = float(lat_match.group(1))/(1000/tCK_ps)
                        if 'total_number_of_retried_requests' in line:
                            lat_match = re.search(r'total_number_of_retried_requests:\s*([\d.]+)', line)
                            if lat_match:
                                total_number_of_retried_requests = int(lat_match.group(1))

                if memory_cycles is not None and avg_latency_ns is not None and total_number_of_writes is not None and total_number_of_reads is not None:
                    time_sec = memory_cycles / mem_clk_freq
                    num_RW = total_number_of_reads + total_number_of_writes
                    bandwidth_GBps = ((bytes_per_RW * num_RW) / time_sec) / 1e9

                    numTotalBytes = num_RW * bytes_per_RW
                    bytes_per_cycle = numTotalBytes / memory_cycles
                    cycles_per_cb = 64 / bytes_per_cycle
                    expected_cycles_per_cb = 8 # tCCDS = 8

                    expected_bandwidth = (1e9 / (tCK_ps/1000)) / 8 * 64 / 1e9

                    if bandwidth_GBps > max_achieved_bandwidth: 
                        max_achieved_bandwidth = bandwidth_GBps

                    data.append((bandwidth_GBps, avg_latency_ns, nop_count, avg_stream_queue, avg_strided_latency_ns, avg_random_read_latency, total_number_of_retried_requests))

    df = pd.DataFrame(data, columns=['Bandwidth (GB/s)', 'Avg Read Latency (ns)', 'NOP Count', 'Avg Stream Queue (ns)', 'Avg Strided Latency (ns)', 'Avg Random Read Latency (ns)', 'Total Number of Retried Requests'])
    df = df.sort_values('NOP Count')
    df['Label'] = label
    all_dfs.append(df)

plt.figure(figsize=(10, 6))

custom_colors = [
    (0.0, "#0000AA"),  
    (1.0, "#AA0000"),  
]

trunc_cmap = LinearSegmentedColormap.from_list("custom_blue_red", custom_colors)


norm = mpl.colors.Normalize(vmin=50, vmax=100)
sm = plt.cm.ScalarMappable(cmap=trunc_cmap, norm=norm)
sm.set_array([])

colors = [trunc_cmap(norm(label_to_reads[df['Label'].iloc[0]])) for df in all_dfs]

for i, df in enumerate(all_dfs):
    plt.plot(df['Bandwidth (GB/s)'], df['Avg Random Read Latency (ns)'],
         linestyle='-', label='_nolegend_', color=colors[i], linewidth=2)

cbar = plt.colorbar(sm, ax=plt.gca(), pad=0.02)
cbar.set_label('% Reads', fontsize=18)
cbar.ax.tick_params(labelsize=18)

# max_bandwidth = (1000000000/((8 * 0.416))) * 64 * num_channels /1e9 # from clock period
max_bandwidth = (4800 * 32/8) / 1000 * num_channels # from data rate

plt.axvline(x=max_bandwidth, color='orange', linestyle='--', linewidth=2, label='Max. Theoretical BW (No Refresh)')
plt.text(max_bandwidth, plt.ylim()[1]*1.05, f'{max_bandwidth:.1f}', ha='center', va='top', fontsize=15, color='orange')

nanoseconds_gone_to_refresh = 329.472
gone_per_refi = (nanoseconds_gone_to_refresh / 3900)
bandwidth_after_refresh = max_bandwidth * (1 - gone_per_refi)

gone_per_refi_jedec_small = (793 * 0.416)/3900
bandwidth_after_refresh_jedec_small = max_bandwidth * (1 - gone_per_refi_jedec_small)

plt.axvline(x=bandwidth_after_refresh_jedec_small, color='green', linestyle='--', linewidth=2, label='Max. Achievable BW (With All-Bank Refresh)')
plt.text(bandwidth_after_refresh_jedec_small, plt.ylim()[1]*1.05, f'{bandwidth_after_refresh_jedec_small:.1f}', ha='center', va='top', fontsize=15, color='green')

plt.xticks(fontsize=12)
plt.yticks(fontsize=13)

plt.tick_params(axis='both', which='major', direction='in', length=6, width=2)

plt.tick_params(axis='both', which='major', labelsize=18)

plt.xlabel('Used memory bandwidth (GB/s)', fontsize=20, labelpad=10)
plt.ylabel('Memory access latency (ns)', fontsize=20, labelpad=10)
plt.grid(True, linestyle='--', linewidth=1.3, alpha=0.7)

legend = plt.legend(fontsize=18, frameon=True, edgecolor='black', facecolor='white', loc='upper center', bbox_to_anchor=(0.5, 1.35))
legend.get_frame().set_linewidth(1.5)

ax = plt.gca() 
for spine in ax.spines.values():
    spine.set_linewidth(2)

plt.tight_layout()
plt.savefig(f'latency_bandwidth_{num_channels}ch.png', dpi=300)
plt.savefig(f'latency_bandwidth_{num_channels}ch.pdf', bbox_inches='tight')
plt.show()

print(f"max bandwidth is {max_achieved_bandwidth}")



