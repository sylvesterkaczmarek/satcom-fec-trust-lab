# SIMD Path Status

The repository distinguishes implementation maturity explicitly and keeps the
public benchmark scope narrow.

| Path | Status | Benchmarked in public repo | What is implemented |
| --- | --- | --- | --- |
| `viterbi-neon` | real | yes | NEON branch-metric preparation on Arm NEON targets, followed by the shared scalar add-compare-select and traceback core |
| `viterbi-sme2` | simplified | yes | shared scalar reference Viterbi decode with no SME2-specific kernel |
| `ldpc-neon` | simplified | no | shared bit-flip reference decoder with no NEON-specific kernel |
| `ldpc-sme2` | simplified | no | shared bit-flip reference decoder with no SME2-specific kernel |

There is no checked-in placeholder SIMD path in the supported replay workflow.

The benchmark harness compares `viterbi-neon` and `viterbi-sme2` on the same
prepared frame window. That makes the harness useful for code-path alignment and
local timing, but it is not evidence of SME2 acceleration because the SME2 path
is still simplified.

Publication-safe wording for this repo:

- `viterbi-neon` is the only checked-in path with NEON-specific implementation
  work.
- `viterbi-sme2` is a comparison entrypoint, not an SME2-optimized kernel.
- The benchmark is a like-for-like local timing comparison over one prepared
  replay frame, not a general performance claim.
