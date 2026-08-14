# SIMD path status

IQ-domain acquisition is the supported architecture-optimization workload.

| Path | Status | Implementation |
| --- | --- | --- |
| Acquisition reference | real scalar oracle | float64 correlation over the complete timing/CFO grid |
| Acquisition NEON | real when compiled for an AArch64 Advanced SIMD target | float32 NEON complex multiply-accumulate over up to 32 consecutive timing hypotheses, with explicit smaller-tile and tail handling |
| Acquisition SME2 | real when compiled and runtime-supported | float32 SME2 VGx4 `FMLA`/`FMLS` into ZA across timing tiles; direct interleaved-IQ loads for consecutive grids and explicit packing for arbitrary grids |
| `viterbi-reference` | real scalar decoder; replay default | scalar branch metrics, add-compare-select, and traceback |
| `viterbi-neon` | partial or reported fallback | NEON branch metrics when available; scalar recurrence and traceback |
| `viterbi-streaming-vector` | historical partial experiment or reported fallback | locally streaming SVE-style branch metrics; no ZA or SME2-specific multi-vector operation; scalar recurrence and traceback |
| LDPC bit-flip reference | simplified reference utility | scalar bit-flip algorithm; no public NEON or SME2 path |

Accelerated acquisition requests report `unavailable` rather than executing a
fallback under an accelerated name. Acquisition SME2 is a genuine ZA-backed
kernel; the historical Viterbi streaming-vector experiment is not.

`benchmarks/results/a83cd53/` is historical evidence for an earlier packed SME2
input path and an earlier NEON baseline. Its JSON remains authoritative for
that source commit, but its timing and workspace values do not describe the
current kernels. No Android timing artifact is included.

`benchmark_acquisition` is the supported benchmark. It verifies candidate
identity and score tolerances before reporting steady-state, per-capture, and
setup-inclusive local timings and memory costs.

The historical FEC timing harness is isolated in
`experiments/viterbi_branch_metrics/`. It compares identical prepared soft
decisions and preserves correctness evidence, but it is not used to support an
SME2 performance claim or an end-to-end FEC acceleration claim.
