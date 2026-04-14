# Architecture

The supported public demo is a single offline replay path:

1. `load_iq_from_file` reads an interleaved float32 IQ recording.
2. `run_front_end` removes DC bias and normalizes RMS level.
3. `soft_demodulate_bpsk` performs integrate-and-dump demodulation at a fixed
   samples-per-symbol setting and scales soft decisions from the observed symbol
   envelope.
4. `find_frames` locates the 16-bit sync word by correlation, tracks any
   second-best candidate, and slices the coded frame.
5. `viterbi_decode_neon` or `viterbi_decode_sme2` decodes the convolutionally
   coded frame. The NEON path uses a checked-in NEON branch-metric kernel, while
   the SME2 path keeps the shared scalar decode core.
6. The decoded bytes are checked with CRC-8.
7. `compute_trust_features` and `compute_trust_score` summarize replay
   confidence from sync quality, sync ambiguity, soft-bit magnitude, weak-bit
   rate, demod clipping, and CRC success.

The host-side entrypoint for this flow is `tools/replay_demo.cpp`, built and
run by `scripts/run_replay_demo.sh`. Decoder alignment and local timing are
handled by `scripts/validate_decoder_alignment.sh` and
`scripts/benchmark_decoder_paths.sh`.

The replay result is intentionally structured. It reports front-end statistics,
demodulation and framing details, decoder identity, trust features, and the
trust-score component weights that produced the final scalar score.
