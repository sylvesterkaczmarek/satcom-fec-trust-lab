# Acquisition benchmarking

`scripts/benchmark_acquisition.sh` runs the reference, NEON, and genuinely
available SME2 acquisition implementations against fixed synthetic workload
classes. JSON written to standard output is authoritative; `--json PATH` and
`--csv PATH` optionally persist the JSON and a compact summary.

```bash
# Full predetermined sweep with default statistical settings.
bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark.json \
  --csv build/acquisition-benchmark.csv

# Include SME2 only on a compiler and host that support it.
SATCOMFEC_ENABLE_SME2=ON bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark-sme2.json
```

An unavailable accelerated implementation is recorded as unavailable and is
not timed. No scalar fallback is reported as NEON or SME2.

## Fixed workloads

These definitions are checked into `tools/acquisition_benchmark.cpp`; they are
not selected after observing results.

| Class | IQ samples | Preamble | Timing hypotheses | CFO hypotheses | Candidate correlations | Complex MACs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| small | 2,048 | 64 | 1,024 | 5 | 5,120 | 327,680 |
| medium | 8,192 | 128 | 4,096 | 9 | 36,864 | 4,718,592 |
| large | 32,768 | 256 | 16,384 | 17 | 278,528 | 71,303,168 |
| very-large | 65,536 | 512 | 32,768 | 25 | 819,200 | 419,430,400 |

Every workload uses a deterministic QPSK preamble, deterministic complex
noise, a 48 ksample/s rate, 250 Hz CFO spacing, and an injected candidate on
the search grid. The workload classes are engineering sweeps, not
mission-derived waveform profiles.

## Method

- The scalar result must recover ground truth before timing starts.
- Each available accelerated result must match the scalar best candidate,
  second-best candidate, candidate count, and score tolerances. A failing path
  is not assigned valid timing results.
- Default timing uses two warm-up rounds, 15 independent samples, and at least
  50 ms per timed sample. Each sample repeats a complete acquisition operation
  until the minimum duration is reached.
- Implementation/mode order is deterministically shuffled before every
  warm-up round and timed sample. Each workload's data and order seeds, plus
  every actual timed order, are reported. A named workload has the same data
  whether it is run alone or as part of the full sweep.
- The scalar translation unit has loop and SLP auto-vectorization disabled
  where the compiler supports those controls. NEON and SME2 target flags are
  confined to their respective translation units.
- `steady-state` includes correlation, magnitude-squared scoring, and top-two
  reduction using validated inputs and precomputed tables. SME2 packing and
  output buffers are preallocated.
- `setup-inclusive` additionally includes acquisition-plan allocation and CFO
  table generation. For SME2 it also includes workspace allocation, input
  packing, and release.

The report provides median, mean, sample standard deviation, minimum, maximum,
nearest-rank p50/p95, raw per-sample latencies, block durations, operation
counts, candidate correlations/s, complex MACs/s, and median-latency speedups
relative to reference and NEON. It also records the commit, dirty-tree state,
timestamp, OS, architecture, CPU model, compiler, source-specific flags,
runtime feature detection, vector width where available, workload definition,
correctness evidence, and implementation actually executed.

## Interpretation limits

This is local process timing. It does not control CPU affinity, frequency,
thermal state, or competing system activity, and it does not measure energy.
Results from one device are not a general architecture claim. Setup-inclusive
and steady-state rankings may differ. The benchmark contains no SVE acquisition
path, so it does not label streaming SVE as SME2 or provide an SVE comparison.

## Legacy decoder experiment

`scripts/benchmark_decoder_paths.sh` remains available as a narrow Viterbi
experiment over one 244-bit replay frame. It reports branch-metric preparation
separately from full decode and verifies decoded-bit alignment. The NEON and
SME2 decoder paths accelerate branch-metric preparation only; add-compare-select
and traceback remain scalar. This decoder experiment is not evidence for
acquisition performance or end-to-end SME2 Viterbi acceleration.
