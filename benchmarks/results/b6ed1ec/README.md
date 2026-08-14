# Acquisition benchmark result: b6ed1ec

This result set contains five independent full benchmark processes. Every raw
JSON report passed reference/NEON/SME2 correctness gating before timing.

## Provenance

- Source commit: `b6ed1ec073ea4406255c3201a39e81ecc67e21fd`
- Working tree at build: clean (`dirty=false`)
- Host-reported CPU: Apple M5 Pro
- Device model: `Mac17,9`
- OS: Darwin `25.6.0`, arm64
- Compiler: Apple Clang `21.0.0 (clang-2100.1.1.101)`
- Common flags: `-O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic -Werror`
- Reference flags: `-fno-vectorize -fno-slp-vectorize`
- NEON flags: `-mcpu=native+nosve+nosve2+nosme+nosme2`
- SME2 control flags: generic common flags, without an SVE/SME target
- SME2 kernel flags: `-mcpu=native+sme2`
- NEON width: 128 bits
- SME streaming vector width: 512 bits

The exact executable arguments, workload definitions, build metadata, feature
detection, execution order, raw samples, and correctness evidence are retained
in each process report. `summary.json` is derived from those reports.

## Timing summary

Latency values are the median of five process-level medians. The speedup range
contains the independently measured `NEON latency / SME2 latency` value from
all five processes. CV is the sample coefficient of variation of the five SME2
run medians.

| Mode | Workload | Reference ms | NEON ms | SME2 ms | SME2 vs NEON range | SME2 CV |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| per-capture | small | 0.234944 | 0.036515 | 0.020136 | 1.772-1.895x | 1.13% |
| per-capture | medium | 3.414089 | 0.499691 | 0.227356 | 2.169-2.203x | 3.65% |
| per-capture | large | 50.140583 | 7.084411 | 3.044657 | 2.282-2.330x | 2.99% |
| per-capture | very-large | 300.094875 | 41.319896 | 17.915806 | 2.268-2.375x | 3.07% |
| steady-state | small | 0.233683 | 0.036072 | 0.020110 | 1.775-1.890x | 0.94% |
| steady-state | medium | 3.412239 | 0.513897 | 0.227151 | 2.171-2.262x | 3.62% |
| steady-state | large | 50.162125 | 7.126286 | 3.053559 | 2.271-2.336x | 3.50% |
| steady-state | very-large | 300.478458 | 41.359958 | 17.725319 | 2.267-2.499x | 2.96% |
| setup-inclusive | small | 0.264834 | 0.066473 | 0.058692 | 1.050-1.210x | 2.63% |
| setup-inclusive | medium | 3.545019 | 0.626735 | 0.372705 | 1.670-1.709x | 5.04% |
| setup-inclusive | large | 51.059125 | 7.622149 | 3.641521 | 2.089-2.130x | 2.47% |
| setup-inclusive | very-large | 301.885958 | 41.975250 | 18.825806 | 2.214-2.268x | 3.74% |

## Memory

These are logical payload bytes. Allocator bookkeeping, stack state, code
pages, and process resident-set size are excluded. The fixed workloads use
consecutive timing hypotheses, so the SME2 kernel reads interleaved IQ directly
and has no packed per-capture input workspace.

| Workload | SME2 reusable plan | SME2 per-capture input | Correlation output | Total SME2 temporary |
| --- | ---: | ---: | ---: | ---: |
| small | 10,792 | 0 | 40,960 | 40,960 |
| medium | 42,056 | 0 | 294,912 | 294,912 |
| large | 166,024 | 0 | 2,228,224 | 2,228,224 |
| very-large | 364,744 | 0 | 6,553,600 | 6,553,600 |

Reference and NEON allocate no corresponding dynamic acquisition workspace.
An arbitrary non-contiguous timing grid requires an additional SME2 packed
input of `timing hypotheses * preamble length * sizeof(ComplexF)` bytes.

## Historical comparison

The fixed workload definitions and timing modes match the historical
[`a83cd53`](../a83cd53/README.md) result. The current eight-vector NEON kernel
has a lower median-of-run-medians in every workload and mode. The current SME2
kernel has lower per-capture and setup-inclusive medians in every workload and
lower large and very-large steady-state medians. Its small and medium
steady-state medians are respectively 16.5% and 14.6% higher than the earlier
packed-input kernel.

This is not a controlled longitudinal hardware study: source code and process
conditions differ, and CPU affinity, frequency, and thermal state were not
controlled. Both directories remain authoritative only for their recorded
source revisions.

## Interpretation

On this recorded host and direct-correlation workload, SME2 latency was lower
than the audited NEON baseline in every fixed workload and timing mode in all
five processes. The small setup-inclusive margin was the narrowest. This is
local evidence, not a general SME2 performance claim.

The large and very-large classes are direct-kernel stress sweeps. For longer
preambles, wider windows, or denser CFO grids, FFT/filter-bank or hierarchical
searches may be algorithmically preferable; those alternatives are outside
this repository.
