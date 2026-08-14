# Acquisition benchmark result: a83cd53

This result set contains five independent full benchmark processes. Each raw
JSON passed reference/NEON/SME2 correctness gating before timing.

## Provenance

- Source commit: `a83cd53ffe153fa69329194174f735d0a972380d`
- Working tree at build: clean (`dirty=false`)
- Host-reported CPU: Apple M5 Pro
- Device model: `Mac17,9`
- OS: Darwin `25.6.0`, arm64
- Compiler: Apple Clang `21.0.0 (clang-2100.1.1.101)`
- Common flags: `-O3 -DNDEBUG -std=c++17`
- Reference flags: `-fno-vectorize -fno-slp-vectorize`
- NEON flags: `-mcpu=native+nosve+nosve2+nosme+nosme2`
- SME2 flags: `-mcpu=native+sme2`
- SME streaming vector width: 512 bits

Command:

```bash
SATCOMFEC_ENABLE_SME2=ON python3 scripts/repeat_acquisition_benchmark.py \
  --output-dir benchmarks/results/a83cd53
```

## Timing summary

Latency values are the median of the five process-level medians. The final
column gives the range of SME2-versus-NEON speedups retained from the five raw
runs.

| Workload | Mode | Reference ms | NEON ms | SME2 ms | SME2 / NEON range |
| --- | --- | ---: | ---: | ---: | ---: |
| small | steady-state | 0.232290 | 0.063281 | 0.017260 | 3.513654-3.689661 |
| small | per-capture | 0.232264 | 0.063225 | 0.051513 | 1.166421-1.247423 |
| small | setup-inclusive | 0.261683 | 0.090301 | 0.086870 | 1.015348-1.050003 |
| medium | steady-state | 3.307802 | 0.856751 | 0.198133 | 4.162036-4.350393 |
| medium | per-capture | 3.295987 | 0.855379 | 0.375940 | 2.202064-2.290040 |
| medium | setup-inclusive | 3.408231 | 0.970989 | 0.507126 | 1.859127-1.923530 |
| large | steady-state | 49.552979 | 12.563406 | 8.872715 | 1.388414-1.468605 |
| large | per-capture | 49.491479 | 12.590271 | 10.323333 | 1.215021-1.261374 |
| large | setup-inclusive | 50.041291 | 13.081240 | 10.356783 | 1.240628-1.285233 |
| very-large | steady-state | 292.617291 | 75.802042 | 56.769625 | 1.313498-1.336533 |
| very-large | per-capture | 291.876208 | 75.972875 | 62.420666 | 1.200065-1.217111 |
| very-large | setup-inclusive | 294.048583 | 76.826458 | 64.144584 | 1.166398-1.210783 |

## SME2 workspace

Bytes are logical payload sizes. Allocator bookkeeping and process resident-set
size are not included.

| Workload | Reusable plan | Per-capture packed IQ | Correlation output | Total temporary |
| --- | ---: | ---: | ---: | ---: |
| small | 10,792 | 524,288 | 40,960 | 565,248 |
| medium | 42,056 | 4,194,304 | 294,912 | 4,489,216 |
| large | 166,024 | 33,554,432 | 2,228,224 | 35,782,656 |
| very-large | 364,744 | 134,217,728 | 6,553,600 | 140,771,328 |

## Files

- `run-01.json` through `run-05.json`: unabridged process reports with raw
  samples, operation counts, correctness, execution order, flags, and memory
- `summary.json`: median/minimum/maximum/spread/CV of run medians and each
  independent SME2-versus-NEON speedup

No CPU affinity, frequency locking, thermal stabilization, or energy
measurement was used. These local values do not establish performance on
other devices or receiver workloads.
