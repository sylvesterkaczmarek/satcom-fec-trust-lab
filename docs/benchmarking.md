# Benchmarking Notes

This repository includes one narrow benchmark:

- `scripts/benchmark_decoder_paths.sh`

It times the checked-in `viterbi-neon` and `viterbi-sme2` entrypoints on the
same prepared replay frame inside one host-side process.

What the benchmark does:

- prepares the replay frame once from the same checked-in IQ input
- runs both decoder entrypoints with the same samples-per-symbol setting
- uses the same framed coded-bit window for both paths
- uses the same shared traceback and state-machine core for both paths
- reports local timing, implementation class, decoded-bit count, and decoded-bit
  checksum for each path

What the benchmark does not prove:

- SME2 acceleration
- device-level performance
- thermal behavior
- cross-platform ranking
- LDPC performance

Implementation maturity in this benchmark:

- `viterbi-neon`: real checked-in NEON branch-metric preparation on Arm NEON
  targets, followed by the shared scalar decode core
- `viterbi-sme2`: simplified comparison path using the shared scalar reference
  Viterbi decode with no SME2-specific kernel

The benchmark JSON is intended to be auditable rather than promotional. It
includes:

- input IQ path
- decoder settings
- prepared-frame metadata
- warmup and timed iteration counts
- compile target
- implementation class and summary for each path
- decoded-bit counts and checksums for alignment

Use `scripts/validate_decoder_alignment.sh` when you want a pass/fail check that
both benchmarked decoder paths operated on the same prepared input and produced
aligned output.
