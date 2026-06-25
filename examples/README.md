# Ramulator2 examples

End-to-end runnable configuration scripts covering every DRAM
standard and frontend the v2.1 API ships. Each script wires the
spec-specific bits explicitly so they can serve as ready-to-fork
templates for your own configurations.

## Per-standard examples

| Script | Spec | Controller | Frontend |
|--------|------|------------|----------|
| `example_config.py` | DDR4 | GenericDDR | SimpleO3 |
| `ddr3_config.py` | DDR3 | GenericDDR | SimpleO3 |
| `ddr5_config.py` | DDR5 | GenericDDR | SimpleO3 |
| `gddr6_config.py` | GDDR6 | GenericDDR | SimpleO3 |
| `hbm2_config.py` | HBM2 | HBM12 | SimpleO3 |
| `hbm3_loadstore_config.py` | HBM3 | HBM34 | LoadStoreTrace |
| `hbm4_config.py` | HBM4 | HBM34 | SimpleO3 |
| `hbm4_rfm_manager_config.py` | HBM4 + RFMManager | HBM34 | SimpleO3 |
| `lpddr5_config.py` | LPDDR5 | LPDDR5 (split-activate) | SimpleO3 |

Run any of them from the repo root, e.g.:

```sh
$ python examples/hbm4_config.py
Controller cycles:     81393
Avg read latency:      133.8 cycles
Read requests:         6
Write requests:        0
Row hits:              2
Row misses:            4
Row conflicts:         0
```

## What's actually different between the examples

Most of the boilerplate is identical — the differences are
spec-specific choices that aren't obvious from a single DDR4
template:

- **HBM2/HBM3/HBM4**: 32-byte transactions, so SimpleO3 needs
  `llc_linesize=32` instead of the default 64.
- **HBM12 vs HBM34 controller**: HBM12 covers HBM1/HBM2; HBM34
  covers HBM3/HBM4. They differ in the dual-bus / Sid handling.
- **HBM**: `AllBank` refresh manager auto-scopes to
  `PseudoChannel` (HBM has no Rank); `PerBank` / `HBM34PerBankRefresh`
  for per-bank refresh.
- **LPDDR5**: dedicated `LPDDR5` controller (split-activate ACT1/ACT2
  protocol).
- **DDR3**: simplest baseline — no rank-scope, no RFM, no split-activate.

## Trace formats

The bundled traces under `examples/traces/`:

| File | Format |
|------|--------|
| `example_inst.trace` | SimpleO3 instruction trace: `<bubbles> <load_addr> [store_addr]` |
| `example_loadstore.trace` | LoadStoreTrace: `LD <addr>` / `ST <addr>` per line |

Both replay cyclically — the SimpleO3 examples stop after
`num_expected_insts` retire; LoadStoreTrace stops after one pass.
