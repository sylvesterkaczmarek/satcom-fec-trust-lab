# SIMD Path Status

The repository distinguishes implementation maturity explicitly:

| Path | Status | What is implemented |
| --- | --- | --- |
| `viterbi-neon` | real | NEON branch-metric preparation on Arm NEON targets, followed by the shared scalar add-compare-select and traceback core |
| `viterbi-sme2` | simplified | shared scalar reference Viterbi decode with no SME2-specific kernel |
| `ldpc-neon` | simplified | shared bit-flip reference decoder with no NEON-specific kernel |
| `ldpc-sme2` | simplified | shared bit-flip reference decoder with no SME2-specific kernel |

There is no checked-in placeholder SIMD path in the supported replay workflow.

The benchmark harness compares `viterbi-neon` and `viterbi-sme2` on the same
prepared frame window. That makes the harness useful for code-path alignment and
local timing, but it is not evidence of SME2 acceleration because the SME2 path
is still simplified.
