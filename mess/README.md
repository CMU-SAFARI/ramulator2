# Reproduce Mess benchmark results in our [publication](https://arxiv.org/pdf/2510.15744)

This can be done in two steps:

1. Run run.py: `python3 run.py`
2. Run plot.py (or use plot.ipynb): `python3 plot.py`

plot.py will print the maximum achieved bandwidth that we 
report as 281.1 GB/s in Section 4 in our paper.

plot.py will generate two identical figures in two file 
formats `latency_bandwidth_16ch.pdf` and `latency_bandwidth_16ch.png`. 
These are Figure 1-c and Figure 3 (they are the same) in our paper.
