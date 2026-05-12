# Benchmarking Notes

This repository includes one narrow benchmark:

- `scripts/benchmark_decoder_paths.sh`

It times the checked-in `viterbi-reference`, `viterbi-neon`, and `viterbi-sme2`
entrypoints on the same prepared replay frame inside one host-side process.

What the benchmark does:

- prepares the replay frame once from the same checked-in IQ input
- runs all decoder entrypoints with the same samples-per-symbol setting
- uses the same framed coded-bit window for all paths
- uses the same shared scalar traceback and state-machine core for all paths
- reports branch-metric preparation timing separately from full decode timing
- reports selected branch-metric implementation, implementation class,
  decoded-bit count, and decoded-bit checksum for each path

What the benchmark does not prove:

- end-to-end SME2 Viterbi acceleration
- a general SME2 speedup result
- device-level performance
- thermal behavior
- cross-platform ranking
- LDPC performance

Implementation maturity in this benchmark:

- `viterbi-neon`: partial Neon path with checked-in Neon branch-metric
  preparation on Arm NEON targets, followed by the shared scalar decode core
- `viterbi-sme2`: partial decoder path with SME2/SME streaming-mode
  branch-metric preparation when compiled with `__ARM_FEATURE_SME2`, followed by
  the shared scalar decode core; non-SME2 builds report scalar fallback for this
  path
- `viterbi-reference`: scalar reference Viterbi decode

Small replay frames can make the SME2 branch-metric path slower than reference
or Neon because setup and streaming-mode overhead may dominate. Treat the output
as local timing for this canned frame only.

The benchmark JSON is intended to be auditable rather than promotional. It
includes:

- input IQ path
- decoder settings
- prepared-frame metadata
- warmup and timed iteration counts
- compile target
- implementation class and summary for each path
- selected branch-metric implementation for each path
- branch-metric preparation time for each path
- full decode time for each path
- decoded-bit counts and checksums for alignment

Use `scripts/validate_decoder_alignment.sh` when you want a pass/fail check that
all benchmarked decoder paths operated on the same prepared input and produced
aligned output.

Use `scripts/check_branch_metrics.sh` when you want a deterministic branch-metric
equivalence check over short, tail, and full-frame-sized inputs.

Use `scripts/verify_arm_paths.sh` to print compiler and architecture details,
build the default portable path, run tests, and try an SME2 build only when the
compiler supports the SME2 target flag and ACLE streaming attribute.
