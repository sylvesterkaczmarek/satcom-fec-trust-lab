# Pocket Satcom architecture

The supported path is a synthetic-IQ receiver replay with IQ-domain acquisition
as its primary synchronization and architecture-optimization workload. The
end-to-end replay and acquisition benchmark use the same reference, Arm NEON,
and Arm SME2 matched-filter implementations described in
`docs/acquisition_design.md`. The standalone `acquisition_demo` remains a
focused correctness tool; acquisition is not isolated from replay.

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
7. `viterbi_decode_reference_path` decodes the convolutionally coded frame by
   default. The optional `viterbi-neon` path uses NEON branch-metric
   preparation when available, followed by the shared scalar recurrence and
   traceback. A historical locally streaming SVE-style branch-metric path is
   retained under `experiments/viterbi_branch_metrics/`; it does not use ZA or
   an SME2-specific multi-vector operation.
8. The decoded bytes are checked with CRC-8.
9. `compute_trust_features` and `compute_trust_score` summarize normalized
   acquisition strength, peak separation, residual uncertainty, soft-decision
   evidence, secondary frame sync, demod clipping, and CRC state.

The host-side entrypoint for this flow is `tools/replay_demo.cpp`, built and
run by `scripts/run_replay_demo.sh`. Decoder alignment remains a functional FEC
check in `scripts/validate_decoder_alignment.sh`; the timing harness is isolated
under `experiments/viterbi_branch_metrics/`.

Acquisition timing is a separate, correctness-gated workload sweep implemented by
`tools/benchmark_acquisition.cpp` and `tools/acquisition_benchmark.cpp`, exposed
through `scripts/benchmark_acquisition.sh`. It correctness-gates each available
implementation before reporting steady-state, per-capture, or setup-inclusive
timing. Per-capture timing reuses the plan and allocations while charging SME2
for sample-major packing of each supplied IQ window. The decoder benchmark is
not used as evidence for acquisition behavior. Checked acquisition timing
evidence, including its host and build metadata, is documented in
`benchmarks/results/a83cd53/`.

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

## Build boundaries

CMake is the authoritative host and NDK build path. Compiler warning and
sanitizer options are common target options; architecture features are not.
The scalar acquisition reference has a dedicated translation unit with loop
and SLP vectorization disabled. NEON and SME2 flags are attached only to their
respective implementation sources. The FEC scalar core is separate from NEON
branch metrics and the historical streaming-vector branch metrics.

Every configured build exports `compile_commands.json`.
`scripts/check_compile_commands.py` verifies the boundaries and fails if an
SME2 target flag contaminates the scalar reference, NEON comparison, or generic
FEC source.
