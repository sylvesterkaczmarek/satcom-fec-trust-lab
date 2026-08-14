# Reproducibility

This repository is intentionally small enough to be rerun from a clean checkout
without extra data downloads or hidden setup.

Publicly supported workflow:

1. Build the host-side tools.
2. Run and verify the scalar acquisition fixtures.
3. Run the baseline replay fixture, including IQ acquisition and alignment.
4. Run impaired, ambiguous, CRC-failed, and no-signal fixtures.
5. Verify the replay output, trust comparison, and downstream FEC alignment.

Exact commands:

```bash
make verify
```

`make verify` starts from fresh build directories and performs fixture hash
verification, a strict-warning portable build, compile-command isolation
checks, CTests, supported replay/trust/acquisition/FEC scripts, Python
regressions, ASan/UBSan CTests, and optional native Arm build probes.

Individual commands remain available:

```bash
make build
make acquisition
make check-acquisition
make check-acquisition-neon
make verify-acquisition-neon
make check-acquisition-sme2
make replay
make replay-impaired
make replay-failed
make check
make compare-trust
make check-metrics
make verify-arm
make benchmark
make test
```

Equivalent script-by-script flow:

```bash
bash scripts/build_host_tools.sh all
bash scripts/run_acquisition_demo.sh
bash scripts/check_acquisition_demo.sh
bash scripts/check_acquisition_neon.sh
bash scripts/verify_acquisition_neon.sh
bash scripts/check_sme2_acquisition.sh
bash scripts/run_replay_demo.sh
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq
bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq
bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq
bash scripts/check_replay_demo.sh
bash scripts/compare_trust_cases.sh
bash scripts/check_branch_metrics.sh
bash scripts/verify_arm_paths.sh
bash scripts/benchmark_acquisition.sh --json build/acquisition-benchmark.json
python3 scripts/repeat_acquisition_benchmark.py \
  --output-dir build/acquisition-repeatability
python3 -m unittest discover -s tests -v
```

Requirements are CMake 3.22+, a C++17 compiler, Python 3, Bash, Make, and `jq`.
The supported build helper always uses CMake; there is no second direct-compiler
build graph that can drift from the documented source flags.

Checked-in fixtures:

- `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_no_signal.json`
- `data/synthetic/canned_replay/preamble_qpsk_256.iq`
- `data/synthetic/acquisition/preamble_qpsk_256.iq`
- `data/synthetic/acquisition/{clean,noisy,frequency_offset,ambiguous,weak_faded}.iq`
- matching JSON ground-truth sidecars for each acquisition capture
- `data/synthetic/fixture_manifest.json`, containing SHA-256 and byte length
  for every generated IQ/JSON file plus each generator's fixture seeds

Generated from source in this repo:

- `scripts/generate_synthetic_iq.py`
- `scripts/generate_acquisition_fixtures.py`
- `scripts/update_fixture_checksums.py`

Each sidecar records `generator`, `seed`, `preamble_seed`, `iq_sha256`, and
`preamble_sha256`. Verify the checked-in bytes without regeneration using
`make verify-fixtures`. `make regenerate` rewrites both fixture families and
then updates the manifest.

What CI verifies:

- portable x86/Linux compilation, strict warnings, CTests, replay, trust,
  acquisition, downstream FEC checks, fixture regeneration, and Python tests
- portable x86 ASan/UBSan compilation and CTest execution
- source-specific compile flags through the exported compile-command database
- native Arm64/Linux reference/NEON execution, numerical equivalence, and NEON
  object instruction evidence
- NDK SME2 compilation plus streaming-mode and ZA VGx4 instruction evidence;
  this lane does not execute SME2 or publish timing
- exact replay timing/CFO recovery before demodulation and payload decode
- Android NDK r29 cross-compilation of the command-line acquisition benchmark,
  including AArch64 ELF, NEON, scalar-isolation, and SME2 instruction checks
- scalar acquisition timing/CFO recovery on five deterministic fixtures
- reference/NEON acquisition equivalence on native Arm64 CI
- explicit NEON-unavailable reporting on portable x86 CI
- explicit SME2-unavailable reporting on portable and Arm64 CI without SME2
- direct NEON kernel equivalence over vector-width, tail, weak-signal, and
  high-amplitude cases
- local SME2 kernel equivalence over preamble, timing-grid, CFO-grid, and
  scalable-vector tail cases when SME2 execution is available
- acquisition benchmark correctness gating, per-capture contract, workspace
  accounting, and JSON/CSV schema coherence on a small fixed workload; x86 CI
  validates the portable path and native Arm64 CI executes NEON
- five-process repeatability helper behavior on the small fixed workload,
  including preservation of each raw report
- deterministic byte-for-byte acquisition fixture regeneration
- healthy, impaired, ambiguous, CRC-failed, and no-signal trust behavior
- reference versus partial-NEON versus historical streaming-vector-or-fallback
  path alignment on the same prepared replay frame
- reference, NEON-or-fallback, and streaming-vector-or-fallback branch-metric
  equivalence on deterministic short and full-frame-sized inputs
- Python host-side regression tests
- golden structured-output subsets for replay, trust comparison, and the
  legacy decoder report

What reproducibility does not mean here:

- no claim of device-level performance reproducibility
- no claim that local acquisition timing establishes a general NEON or SME2
  speedup
- no claim that independent process runs control CPU frequency or thermal state
- no claim of end-to-end SME2 Viterbi acceleration
- no live RF capture path
- no mission-derived waveform fidelity claim
