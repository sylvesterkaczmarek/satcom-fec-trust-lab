# Architecture

The supported public demo is a single offline replay path:

1. `load_iq_from_file` reads an interleaved float32 IQ recording.
2. `run_front_end` removes DC bias and normalizes RMS level.
3. `soft_demodulate_bpsk` performs integrate-and-dump demodulation at a fixed
   samples-per-symbol setting.
4. `find_frames` locates the 16-bit sync word by correlation and slices the
   coded frame.
5. `viterbi_decode_neon` or `viterbi_decode_sme2` decodes the convolutionally
   coded frame. The NEON path uses a checked-in NEON branch-metric kernel, while
   the SME2 path keeps the shared scalar decode core.
6. The decoded bytes are checked with CRC-8.
7. `compute_trust_features` and `compute_trust_score` summarize replay
   confidence from sync quality, soft-bit magnitude, and CRC success.

The host-side entrypoint for this flow is `tools/replay_demo.cpp`, built and
run by `scripts/run_replay_demo.sh`. Decoder alignment and local timing are
handled by `scripts/validate_decoder_alignment.sh` and
`scripts/benchmark_decoder_paths.sh`.
