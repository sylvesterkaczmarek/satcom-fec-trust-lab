# SIMD Path Status

The repository distinguishes implementation maturity explicitly and keeps the
public benchmark scope narrow.

| Path | Status | Benchmarked in public repo | What is implemented |
| --- | --- | --- | --- |
| Acquisition reference | real scalar oracle | yes | scalar double-precision correlation across the complete timing/CFO grid |
| Acquisition NEON | real target-specific kernel | yes when compiled | float32 NEON complex multiply-accumulate with explicit tail handling |
| Acquisition SME2 | real when explicitly compiled and runtime-supported | yes when executable | float32 SME2 VGx4 `FMLA`/`FMLS` into ZA across four scalable vectors of timing hypotheses; setup, scoring, and ranking remain scalar |
| `viterbi-neon` | partial | yes | Neon branch-metric preparation gated by `__ARM_NEON` / `__ARM_NEON__`, followed by the shared scalar add-compare-select and traceback core |
| `viterbi-sme2` | partial decoder path; real branch-metric kernel when compiled for SME2, fallback otherwise | yes | SME2/SME streaming-mode branch-metric preparation gated by `__ARM_FEATURE_SME2`; non-SME2 builds use scalar branch metrics |
| `viterbi-reference` | real scalar reference | yes | scalar branch-metric preparation plus shared scalar add-compare-select and traceback core |
| LDPC bit-flip reference | real simplified algorithm | no | small reference bit-flip decoder; no public NEON or SME2 LDPC path |

Unavailable acquisition paths report `unavailable` rather than running a
scalar fallback under an accelerated label. `benchmark_acquisition` verifies
candidate identity and score tolerances before timing, then reports local
measurements without embedding a speedup conclusion.

The legacy decoder harness compares `viterbi-reference`, `viterbi-neon`, and
`viterbi-sme2` on the same prepared frame window. It reports branch-metric
preparation timing separately from full decode timing. Full decode timing still
includes the shared scalar add-compare-select and traceback core, so it is not
evidence of end-to-end SME2 Viterbi acceleration.

`scripts/check_branch_metrics.sh` reports which branch-metric implementation was
selected: reference, NEON, SME2, or fallback.

Publication-safe wording for this repo:

- `viterbi-neon` is a partial Neon implementation limited to branch-metric
  preparation.
- `viterbi-sme2` is a partial decoder path with a real SME2 branch-metric
  implementation only in builds where `__ARM_FEATURE_SME2` is available;
  otherwise it is a reported fallback.
- `viterbi-reference` is the scalar reference Viterbi path.
- Viterbi add-compare-select and traceback remain scalar in every public path.
- The decoder benchmark is a like-for-like local timing comparison over one
  prepared replay frame, not a general performance claim.
- Small replay frames can make SME2 branch-metric preparation slower when
  setup and streaming-mode overhead dominate.
