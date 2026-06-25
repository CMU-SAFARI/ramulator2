# HBM family product matrix

Quick reference for which `org_preset` × `timing_preset` combinations
map to which real-world HBM products. Useful for picking the right
in-tree preset when modeling a specific accelerator / GPU / server SKU.

## HBM1

| org_preset | timing_preset | per-pkg | Used in |
|---|---|---|---|
| `HBM1_4Gb` | `HBM1_1Gbps` | 0.5 GB | AMD Fury / AMD Radeon Pro Duo |
| `HBM1_4Gb` | `HBM1_1600Mbps` | 0.5 GB | Tesla P100 (field-binned) |
| `HBM1_4Gb` | `HBM1_2Gbps` | 0.5 GB | Late-life HBM1 silicon |

## HBM2 / HBM2E

| org_preset | timing_preset | per-pkg | Used in |
|---|---|---|---|
| `HBM2_8Gb` | `HBM2_2400Mbps` | 8 GB | NVIDIA Tesla V100 |
| `HBM2_8Gb` | `HBM2_3200Mbps` | 8 GB | NVIDIA A100 40GB (per stack) |
| `HBM2_16Gb` | `HBM2_3600Mbps` | 16 GB | NVIDIA A100 80GB / Samsung Flashbolt |

## HBM3 / HBM3E

| org_preset | timing_preset | per-pkg | Used in |
|---|---|---|---|
| `HBM3_16Gb_8hi` | `HBM3_6400Mbps` | 16 GB | NVIDIA H100 80GB |
| `HBM3_16Gb_12hi` | `HBM3_8000Mbps` | 24 GB | NVIDIA H200 (HBM3E first wave) |
| `HBM3_32Gb_12hi` | `HBM3_9200Mbps` | 48 GB | NVIDIA H200 (HBM3E gen-2) |
| `HBM3_32Gb_12hi` | `HBM3_9600Mbps` | 48 GB | NVIDIA B200 (HBM3E gen-3) |
| `HBM3_32Gb_12hi` | `HBM3_12800Mbps` | 48 GB | Micron HBM3 Gen2 production |

## HBM4

| org_preset | timing_preset | per-pkg | Used in |
|---|---|---|---|
| `HBM4_32Gb_8Hi` | `HBM4_8000Mbps` | 32 GB | HBM4 reference (JEDEC base) |
| `HBM4_32Gb_8Hi` | `HBM4_9600Mbps` | 32 GB | HBM4 first-wave (SK hynix) |
| `HBM4_32Gb_12Hi` | `HBM4_12800Mbps` | 48 GB | HBM4 second-wave production |
| `HBM4_32Gb_16Hi` | `HBM4_14400Mbps` | 64 GB | HBM4 third-wave target |
| `HBM4_64Gb_8Hi` | `HBM4_16000Mbps` | 64 GB | HBM4E forward-looking |

## Notes

- "per-pkg" = total package capacity (`density × Hi / 8`, in GB).
- For HBM3/HBM4, the in-tree `sid` value encodes stack height
  (`sid = Hi / 4`).
- HBM controllers: HBM1/HBM2 → `HBM12`. HBM3/HBM4 → `HBM34`.
- Refresh manager: `HBM34PerBankRefresh` for HBM3/4 PerBank;
  `AllBank` auto-scopes to `PseudoChannel` on HBM2/3/4 and to
  `Channel` on HBM1.
