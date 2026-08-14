# Acquisition benchmarking

`scripts/benchmark_acquisition.sh` runs the reference, NEON, and genuinely
available SME2 acquisition implementations against fixed synthetic workload
classes. JSON written to standard output is authoritative; `--json PATH` and
`--csv PATH` optionally persist the JSON and a compact summary.

The same correctness-gated harness can be built as a minimal `arm64-v8a`
Android command-line executable and run through ADB. Build/runtime feature
gating and exact commands are documented in
[`docs/android_benchmark.md`](android_benchmark.md). No mobile result is
included unless it was retrieved from an actual device run.

```bash
# Full predetermined sweep with default statistical settings.
bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark.json \
  --csv build/acquisition-benchmark.csv

# Include SME2 only on a compiler and host that support it.
SATCOMFEC_ENABLE_SME2=ON bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark-sme2.json

# Preserve five independent process reports plus a summary of run medians.
SATCOMFEC_ENABLE_SME2=ON python3 scripts/repeat_acquisition_benchmark.py \
  --output-dir build/acquisition-repeatability
```

An unavailable accelerated implementation is recorded as unavailable and is
not timed. No scalar fallback is reported as NEON or SME2.

## Tracked result

`benchmarks/results/a83cd53/` is the only checked performance result set. It
contains five independent clean-tree runs of source commit
`a83cd53ffe153fa69329194174f735d0a972380d` on an Apple M5 Pro (`Mac17,9`),
Darwin 25.6.0 arm64, using Apple Clang 21.0.0. The report records 128-bit NEON,
runtime SME2 support, and a 512-bit streaming vector length.

For the operational `per-capture` contract, median-of-run-median latency was:

| Workload | Reference ms | NEON ms | SME2 ms | NEON latency / SME2 latency range |
| --- | ---: | ---: | ---: | ---: |
| small | 0.232264 | 0.063225 | 0.051513 | 1.166-1.247x |
| medium | 3.295987 | 0.855379 | 0.375940 | 2.202-2.290x |
| large | 49.491479 | 12.590271 | 10.323333 | 1.215-1.261x |
| very-large | 291.876208 | 75.972875 | 62.420666 | 1.200-1.217x |

SME2 had lower latency than NEON in every recorded workload/mode combination
on that host. The margin was not uniform: small setup-inclusive timing was
1.015-1.050x, while medium per-capture timing was 2.202-2.290x. This result set
does not show a crossover to an SME2 loss within the four fixed workloads.

The result also records a substantial sample-major workspace: total temporary
SME2 payload ranges from 565,248 bytes for `small` to 140,771,328 bytes for
`very-large`. See the result directory for raw samples, setup-inclusive and
steady-state values, execution order, and correctness records.

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
- `per-capture` reuses the acquisition plan and all realistic allocations, but
  cycles between two deterministic prevalidated IQ windows. Reference and NEON
  read the supplied interleaved IQ directly. SME2 sample-major packing for each
  supplied window is inside the timed operation.
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

The `build` object also records the CMake version and generator, compiler path,
compiler ID and target, target system/processor, warning and sanitizer options,
and the exact source files represented by each implementation label. These
fields describe the binary that emitted the report; they do not replace the
tracked `compile_commands.json` separation check.

## Fairness checks

- All implementations receive one shared timing/CFO hypothesis plan and the
  same deterministic capture set.
- NEON and SME2 use bitwise-identical precomputed float32 weights and float32
  accumulation. The scalar correctness oracle intentionally retains float64
  weights and accumulation.
- Every path includes magnitude-squared scoring and top-two selection. SME2's
  correlation-output writes and scalar reduction remain timed.
- No unavailable accelerated implementation is timed, and the reported
  implementation name must match the requested path.
- Reference auto-vectorization controls and NEON/SME2 target flags remain
  translation-unit specific. In an SME2 build, the NEON translation unit uses
  the corresponding base target with SVE and SME explicitly disabled where the
  compiler accepts it; otherwise configuration requires a NEON-only Armv8
  target. The report records both source flags.
- `scripts/verify_sme2_acquisition_assembly.sh` verifies the ZA VGx4 SME2
  instructions and separately confirms NEON arithmetic without checked
  SVE/SME instruction patterns in the comparison object.
- Every timed block consumes a result fingerprint through a volatile sink, and
  valid implementation/mode blocks are shuffled with the reported seed.

## Memory accounting

Each workload reports common capture bytes and actual shared-plan vector
capacity. Each implementation reports logical reusable-plan payload,
per-capture workspace payload, correlation-output payload, total temporary
workspace payload, and measured allocated capacity when that workspace is
instantiated. The values exclude allocator bookkeeping, stack objects, process
resident-set size, and code pages.

The SME2 sample-major workspace scales with
`2 * preamble_length * timing_hypotheses * sizeof(float)`. Its correlation
output scales with
`2 * CFO_hypotheses * timing_hypotheses * sizeof(float)`. Reference and NEON
do not allocate corresponding dynamic workspaces in the current implementation.

## Independent process runs

`scripts/repeat_acquisition_benchmark.py` requires at least five process runs.
It stores each raw JSON separately and writes `summary.json` with the median,
minimum, maximum, spread, and coefficient of variation of the run medians for
each workload/mode/implementation. SME2 entries retain the SME2-versus-NEON
speedup from every independent run. Raw reports remain authoritative; the
summary does not replace them.

To reuse an existing configured benchmark without rebuilding, identify that
build explicitly:

```bash
python3 scripts/repeat_acquisition_benchmark.py \
  --skip-build \
  --build-dir build/my-acquisition-build \
  --output-dir build/acquisition-repeatability
```

`--build-dir` accepts repository-relative or absolute paths and takes
precedence over `SATCOMFEC_BUILD_DIR` and architecture-mode inference. A
`--skip-build` run fails before timing if the expected
`benchmark_acquisition` executable is absent or not executable.

Tracked local reports live under `benchmarks/results/`. Hardware identity,
commit, dirty-tree state, compiler, flags, raw samples, and correctness status
in those JSON files are authoritative.

## Interpretation limits

This is local process timing. It does not control CPU affinity, frequency,
thermal state, or competing system activity, and it does not measure energy.
Results from one device are not a general architecture claim. The checked
Apple M5 Pro result supports only the host/workload statements above.
Steady-state, per-capture, and setup-inclusive rankings may differ. Independent
process runs do not control thermal state or frequency. The benchmark contains no SVE
acquisition path, so it does not label streaming SVE as SME2 or provide an SVE
comparison.

## Legacy decoder experiment

`experiments/viterbi_branch_metrics/run.sh` preserves a narrow historical
experiment over one 244-bit replay frame. It reports branch-metric preparation
separately from full decode and verifies decoded-bit alignment. Its third path
uses locally streaming SVE-style operations, not ZA or an SME2-specific
multi-vector operation. Add-compare-select and traceback remain scalar. This
experiment is not evidence for acquisition performance, genuine SME2 Viterbi
acceleration, or end-to-end FEC acceleration.
