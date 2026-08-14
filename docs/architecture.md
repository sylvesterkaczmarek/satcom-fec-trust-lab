# Architecture

The repository has one end-to-end replay workflow and one acquisition benchmark
harness. Both use the same reference, Arm NEON, and Arm SME2 matched-filter
implementations described in `docs/acquisition_design.md`. The standalone
`acquisition_demo` remains a focused correctness tool; acquisition is no longer
isolated from replay.

The replay workflow is:

1. `load_iq_from_file` reads an interleaved float32 IQ recording.
2. `run_front_end` removes DC bias and normalizes RMS level.
3. `acquire_and_align_replay_frame` searches valid frame-start timing offsets
   and five CFO hypotheses against the known 256-sample QPSK preamble.
4. The selected complex correlation supplies timing, CFO, and carrier phase.
   The pipeline rejects weak or insufficiently separated peaks, then corrects
   CFO and phase for the aligned frame.
5. `soft_demodulate_bpsk` demodulates only that aligned frame.
6. `find_frames` checks that the legacy 16-bit soft sync is at offset zero and
   slices the coded bits. It is a secondary consistency check, not the primary
   acquisition mechanism.
7. `viterbi_decode_reference_path`, `viterbi_decode_neon`, or
   `viterbi_decode_sme2` decodes the convolutionally coded frame. The Neon path
   uses checked-in Neon branch-metric preparation when `__ARM_NEON` or
   `__ARM_NEON__` is available. The SME2 path uses SME2/SME streaming-mode
   branch-metric preparation only when built for a suitable Armv9 SME2 target
   with `__ARM_FEATURE_SME2`. The Viterbi trellis recurrence and traceback
   remain scalar in all paths.
8. The decoded bytes are checked with CRC-8.
9. `compute_trust_features` and `compute_trust_score` summarize normalized
   acquisition strength, peak separation, residual uncertainty, soft-bit
   evidence, secondary sync, demod clipping, and CRC state.

The host-side entrypoint for this flow is `tools/replay_demo.cpp`, built and
run by `scripts/run_replay_demo.sh`. Decoder alignment and local timing are
handled by `scripts/validate_decoder_alignment.sh` and
`scripts/benchmark_decoder_paths.sh`.

Acquisition timing is a separate workload sweep implemented by
`tools/benchmark_acquisition.cpp` and `tools/acquisition_benchmark.cpp`, exposed
through `scripts/benchmark_acquisition.sh`. It correctness-gates each available
implementation before reporting steady-state, per-capture, or setup-inclusive
timing. Per-capture timing reuses the plan and allocations while charging SME2
for sample-major packing of each supplied IQ window. The decoder benchmark is
not used as evidence for acquisition behavior.

For Android, `SATCOMFEC_ANDROID_BENCHMARK_ONLY=ON` narrows the CMake graph to
the acquisition sources and `benchmark_acquisition`. The NDK build keeps
reference and NEON code at baseline `arm64-v8a`; only the SME2 translation unit
receives an SME2 target flag. Runtime `AT_HWCAP2` gating prevents entry into the
SME2 kernel on unsupported devices. The executable runs directly through ADB
and does not require an APK or JNI bridge.

The replay result reports acquisition implementation and candidate evidence,
synthetic ground-truth errors when available, front-end/demodulation/framing
details, decoder identity, and the trust-score components. Ground truth is
reported after the search and is never used to select a candidate.
