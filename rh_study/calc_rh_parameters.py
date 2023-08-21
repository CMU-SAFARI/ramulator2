from math import ceil, floor

def get_rh_parameters(mitigation, tRH):
    if(mitigation == "PARA"):
        threshold = 1 - (10**-15)**(1/tRH)
        return threshold
    elif(mitigation == "TWiCe-Ideal"):
        tREFW = 64000000
        tREFI = 7800
        twice_rh_threshold = int(floor(tRH / 4))
        twice_pruning_interval_threshold = twice_rh_threshold / (tREFW / tREFI)
        return twice_rh_threshold, twice_pruning_interval_threshold
    elif(mitigation == "Graphene"):
        tREFW = 64000000
        tRC = 55
        k = 1
        num_table_entries = int(ceil(2*(tREFW/tRC)/tRH * ((k+1)/(k)) - 1))
        activation_threshold = int(floor(tRH / (2*(k+1))))
        reset_period_ns = int(tREFW / k)
        return num_table_entries, activation_threshold, reset_period_ns
    elif(mitigation == "OracleRH"):
        return tRH
    elif(mitigation == "Hydra"):
        hydra_tracking_threshold = int(floor(tRH / 2))
        hydra_group_threshold = int(floor(hydra_tracking_threshold * 4 / 5))
        hydra_row_group_size = 128
        hydra_reset_period_ns = 64000000
        hydra_rcc_num_per_rank = 4096
        hydra_rcc_policy = "RANDOM"
        return hydra_tracking_threshold, hydra_group_threshold, hydra_row_group_size, hydra_reset_period_ns, hydra_rcc_num_per_rank, hydra_rcc_policy
    elif(mitigation == "RRS"):
        tREFW = 64000000
        tRC = 55
        reset_period_ns = tREFW
        rss_threshold = int(floor(tRH / 6))
        num_hrt_entries = int(ceil((tREFW/tRC)/rss_threshold))
        num_rit_entries = int(ceil((tREFW/tRC)/rss_threshold))*2
        return num_hrt_entries, num_rit_entries, rss_threshold, reset_period_ns
