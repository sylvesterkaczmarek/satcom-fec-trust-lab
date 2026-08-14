# Satcom FEC Trust Lab

![Satcom FEC Trust Lab](assets/social/github-social-card-satcom-fec-trust-lab.png)

This repository is a satcom signal-processing project built around deterministic
synthetic IQ fixtures. The supported host replay path performs complex
IQ timing/CFO acquisition before aligned BPSK demodulation, Viterbi decode,
CRC, and structured trust diagnostics. The same scalar reference, Arm NEON,
and explicitly enabled Arm SME2 acquisition kernels are also available through
a standalone correctness tool and benchmark harness.

The public repo provides scoped host-side acquisition and replay demos plus a
minimal Android/ADB native acquisition benchmark. It does not provide a
supported Android app or full SME2-optimized Viterbi decoder.

## Repo status

- Publication-safe today: the IQ acquisition search and host-side canned
  replay flow
- Included validation: host-side automated tests, host CI, and an Android NDK
  command-line benchmark build path
- Not included: Android app packaging, JNI/mobile replay wiring, checked-in
  mobile performance results, SME2-accelerated Viterbi trellis recurrence or
  traceback, live RF capture,
  published performance conclusions, or thermal claims

## Supported scope

- One host-side replay path from checked-in IQ through preamble acquisition,
  CFO/phase compensation, aligned demodulation, decode, CRC, and trust output
- Scalar reference, Arm NEON, and opt-in Arm SME2 acquisition paths over
  4,096-sample IQ windows, 3,841 timing hypotheses, 9 CFO hypotheses, and a
  256-sample complex preamble; unavailable accelerated paths do not use a
  labeled scalar fallback
- One correctness-gated acquisition workload sweep with fixed sizes,
  steady-state, per-capture, and setup-inclusive modes, plus structured local
  timing and workspace accounting
- One legacy decoder-path timing comparison across `viterbi-reference`,
  `viterbi-neon`, and `viterbi-sme2`
- One healthy, impaired, ambiguous, CRC-failed, and no-signal trust comparison
  using checked-in synthetic fixtures

## What is included

- A checked-in synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- Five deterministic acquisition captures and ground-truth metadata under
  `data/synthetic/acquisition/`
- Recorded generator seeds and SHA-256 fixture hashes in each metadata sidecar
  and `data/synthetic/fixture_manifest.json`
- A checked-in impaired synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- A checked-in CRC-failing synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- A competing-peak fixture at
  `data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq`
- A noise-only rejection fixture at
  `data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq`
- The replay acquisition preamble at
  `data/synthetic/canned_replay/preamble_qpsk_256.iq`
- Metadata for that recording at
  `data/synthetic/canned_replay/demo_conv_bpsk.json`
- Metadata for the impaired recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- Metadata for the failed recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- Purpose-built host-side sources under `src/`
- A top-level `CMakeLists.txt` for host builds
- A host-side runner at `scripts/run_replay_demo.sh`
- An acquisition runner at `scripts/run_acquisition_demo.sh`
- A host-side build helper at `scripts/build_host_tools.sh`
- A verification script at `scripts/check_replay_demo.sh`
- An acquisition fixture check at `scripts/check_acquisition_demo.sh`
- A trust comparison script at `scripts/compare_trust_cases.sh`
- A decoder alignment check at `scripts/validate_decoder_alignment.sh`
- A branch-metric equivalence check at `scripts/check_branch_metrics.sh`
- An acquisition benchmark harness at `scripts/benchmark_acquisition.sh`
- Android NDK/ADB benchmark helpers at `scripts/build_android_benchmark.sh`
  and `scripts/run_android_benchmark.sh`, plus build-only ELF/instruction
  verification at `scripts/verify_android_benchmark_build.sh`
- A five-process repeatability helper at
  `scripts/repeat_acquisition_benchmark.py`
- A legacy decoder microbenchmark at `scripts/benchmark_decoder_paths.sh`
- Automated tests at `tests/test_host_replay.py`
- Reference/NEON/SME2 acquisition tests at `tests/test_acquisition.py`
- Golden structured-output subsets under `tests/golden/`
- Host CI at `.github/workflows/host-replay.yml`
- Native C++ modules for front-end processing, framing, Viterbi decode, a small
  LDPC-style bit-flip decoder, reference/NEON/SME2 acquisition, and trust scoring

## What the replay demo does

Each signal-bearing replay capture contains leading noise, a known 256-sample
QPSK preamble, and an oversampled BPSK frame with controlled timing, carrier
phase, and CFO. The frame carries `SATCOM DEMO OK`, a CRC-8 byte, a rate-1/2
`(7, 5)` convolutional code, and a 16-bit secondary sync word.

The runner normalizes the full capture, searches valid timing/CFO hypotheses,
rejects weak or insufficiently separated peaks, compensates the selected CFO
and carrier phase, and demodulates only the aligned frame. The 16-bit sync check
then verifies offset zero before the existing Viterbi/CRC path runs.

The replay runner reports:

- decoded text
- CRC result
- requested and selected acquisition implementation
- detected timing/CFO, raw and normalized peak evidence, and peak separation
- synthetic ground-truth timing/CFO error when a metadata sidecar is present
- front-end normalization statistics
- demodulation and framing statistics
- trust features, trust-score breakdown, and a trust assessment band

The checked-in synthetic asset set contains five scenarios:

- `healthy`: exact acquisition and clean payload decode
- `impaired`: exact acquisition and decode with weaker signal evidence
- `ambiguous`: exact acquisition and decode despite a competing preamble peak
- `failed`: exact acquisition followed by coded-data corruption and CRC failure
- `no_signal`: acquisition rejection before demodulation

## Quick start

Requirements for the supported quick start:

- `bash`
- `make`
- `c++` with C++17 support
- `cmake` 3.22 or newer
- `python3`
- `jq` for the validation scripts

Complete clean-checkout correctness verification:

```bash
make verify
```

Short interactive path:

```bash
make build
make acquisition
make check-acquisition
make check-acquisition-neon
make verify-acquisition-neon
make replay
make check
```

Equivalent direct commands:

```bash
bash scripts/build_host_tools.sh all
bash scripts/run_acquisition_demo.sh
bash scripts/check_acquisition_demo.sh
bash scripts/check_acquisition_neon.sh
bash scripts/verify_acquisition_neon.sh
bash scripts/run_replay_demo.sh
bash scripts/check_replay_demo.sh
```

Common follow-on commands:

```bash
make replay-impaired
make replay-ambiguous
make replay-failed
make replay-no-signal
make compare-trust
make align
make check-metrics
make verify-fixtures
make verify-arm
make benchmark
make test
```

`make benchmark` runs the predetermined acquisition workload sweep. Persist
the authoritative JSON and optional CSV summaries with:

```bash
bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark.json \
  --csv build/acquisition-benchmark.csv
```

Run five independent full processes without discarding the raw reports:

```bash
python3 scripts/repeat_acquisition_benchmark.py \
  --output-dir build/acquisition-repeatability
```

For native acquisition timing on an `arm64-v8a` Android device, see
[docs/android_benchmark.md](docs/android_benchmark.md). This path builds a
command-line executable for `adb shell`; it is not an Android application.

On a host and compiler with SME2 support, build and verify the ZA-based
acquisition path explicitly:

```bash
SATCOMFEC_ENABLE_SME2=ON bash scripts/run_acquisition_demo.sh \
  data/synthetic/acquisition/clean.iq \
  data/synthetic/acquisition/clean.json sme2
SATCOMFEC_ENABLE_SME2=ON bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark-sme2.json
bash scripts/check_sme2_acquisition.sh --require-sme2
bash scripts/verify_sme2_acquisition_assembly.sh
```

Regenerate the checked-in synthetic fixtures:

```bash
make regenerate
```

`make regenerate` rewrites both fixture families and their tracked checksum
manifest. `make verify-fixtures` verifies the recorded seeds and SHA-256 values
without changing repository files.

Run the alternate decoder entrypoint:

```bash
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk.iq viterbi-sme2
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk.iq viterbi-reference
```

The legacy decoder benchmark is intentionally narrow. It compares the checked-in
`viterbi-reference`, `viterbi-neon`, and `viterbi-sme2` entrypoints on the same
prepared replay frame inside one process and reports local timing plus
decoded-bit alignment data. `viterbi-sme2` uses SME2/SME streaming mode only for
branch-metric preparation when `__ARM_FEATURE_SME2` is available; otherwise it
reports fallback behavior.

Benchmark results are local timing only. Small replay frames can make the SME2
branch-metric path slower than reference or Neon because setup and streaming-mode
overhead may dominate. Do not present these numbers as a general SME2 speedup
result.

Run that legacy experiment explicitly with `make benchmark-decoder-legacy`.
It is not used as evidence for acquisition performance.

The supported host quick start does not use Gradle. The separate Android path
uses the NDK CMake toolchain to produce an ADB command-line benchmark; no Gradle
wrapper or APK build is included.

## Example sessions

Scalar acquisition:

```bash
bash scripts/run_acquisition_demo.sh | jq '{scenario, detected_timing_offset, detected_cfo_hz, peak_ratio, acquisition_success, implementation}'
```

Example output:

```json
{
  "scenario": "clean",
  "detected_timing_offset": 768,
  "detected_cfo_hz": 0,
  "peak_ratio": 27.193396,
  "acquisition_success": true,
  "implementation": "reference"
}
```

The acquisition design, score definition, fixture parameters, and correctness
criteria are documented in
[docs/acquisition_design.md](docs/acquisition_design.md).
The SME2 data layout, ZA accumulation, streaming boundary, and validation are
documented in [docs/sme2_acquisition.md](docs/sme2_acquisition.md).

Healthy replay:

```bash
bash scripts/run_replay_demo.sh | jq \
  '{decoded_text, crc_ok, acquisition, trust_score, trust_assessment}'
```

Example output:

```json
{
  "decoded_text": "SATCOM DEMO OK",
  "crc_ok": true,
  "acquisition": {
    "selected_implementation": "reference",
    "acquisition_success": true,
    "detected_timing_offset": 192,
    "detected_cfo_hz": 250,
    "normalized_peak": 0.996778,
    "normalized_peak_separation": 0.955311,
    "confidence": 0.953771,
    "confidence_calibrated": false
  },
  "trust_score": 0.988191,
  "trust_assessment": {
    "band": "high-confidence",
    "weak_soft_bits": false,
    "ambiguous_acquisition": false,
    "acquisition_rejected": false,
    "ambiguous_sync": false,
    "demod_clipping": false,
    "crc_not_evaluated": false,
    "crc_failed": false
  }
}
```

Replay trust comparison:

```bash
bash scripts/compare_trust_cases.sh
```

Example output:

```json
{
  "healthy": {
    "trust_score": 0.988191,
    "trust_band": "high-confidence",
    "acquisition_confidence": 0.953771
  },
  "impaired": {
    "trust_score": 0.907839,
    "trust_band": "guarded",
    "acquisition_confidence": 0.832182
  },
  "ambiguous": {
    "trust_score": 0.776973,
    "trust_band": "guarded",
    "ambiguous_acquisition": true,
    "acquisition_confidence": 0.111134
  },
  "failed": {
    "trust_score": 0.35,
    "trust_band": "low-confidence",
    "error": "CRC mismatch"
  },
  "no_signal": {
    "trust_score": 0.067797,
    "trust_band": "low-confidence",
    "acquisition_success": false,
    "crc_not_evaluated": true
  },
  "comparison": {
    "trust_score_order_ok": true,
    "ambiguous_peak_detected": true,
    "no_signal_rejected_before_demod": true
  }
}
```

## Arm implementation paths

Acquisition:

- Reference uses scalar double-precision complex accumulation and is the
  correctness oracle.
- NEON uses four-lane float32 complex multiply-accumulate in a separate
  translation unit.
- SME2 packs timing hypotheses into four scalable vectors and uses SME2 VGx4
  `FMLA`/`FMLS` operations to accumulate real and imaginary correlations in ZA.
  Workspace packing, score calculation, and top-two selection remain scalar.
- An unavailable NEON or SME2 request fails explicitly; neither path silently
  executes the reference kernel.

Viterbi replay:

- Reference path: `viterbi-reference` uses scalar branch-metric preparation,
  scalar add-compare-select, and scalar traceback.
- Neon path: `viterbi-neon` uses Neon intrinsics for branch-metric preparation
  when `__ARM_NEON` or `__ARM_NEON__` is available. Add-compare-select and
  traceback remain scalar.
- SME2 path: `viterbi-sme2` is a partial decoder path. It uses SME2/SME
  streaming-mode branch-metric preparation only when the code is built for a
  suitable Armv9 SME2 target and `__ARM_FEATURE_SME2` is defined.
  Add-compare-select and traceback remain scalar.
- Auto-selection behavior: the replay CLI selects the named decoder path. Within
  the Neon and SME2 paths, branch-metric preparation reports `neon`, `sme2`, or
  `fallback` depending on the compile target. Fallback means scalar reference
  branch metrics, not hardware acceleration.

LDPC support is limited to the small bit-flip reference helper; there is no
public LDPC Neon or SME2 path.

## SME2 verification notes

Acquisition implementation and verification:

- SME2 source: `src/acquisition/acquisition_sme2.cpp` and
  `src/acquisition/acquisition_sme2.h`
- Shared plan and API: `src/acquisition/acquisition_plan.cpp` and
  `src/acquisition/acquisition_runner.cpp`
- Build flag: `SATCOMFEC_ENABLE_SME2=ON`
- Correctness command:
  `bash scripts/check_sme2_acquisition.sh --require-sme2`
- Instruction command: `bash scripts/verify_sme2_acquisition_assembly.sh`

Viterbi branch-metric implementation and verification:

- SME2 source: `src/fec/branch_metrics_sme2.cpp` and
  `src/fec/branch_metrics_sme2.h`
- Decoder metadata and scalar Viterbi core:
  `src/fec/convolutional_codec.cpp` and `src/fec/convolutional_codec.h`
- SME2 wrapper: `src/fec/viterbi_decoder_sme2.cpp`
- CMake build command:
  `cmake -S . -B build/sme2 -DSATCOMFEC_ENABLE_SME2=ON`
- CMake helper command:
  `SATCOMFEC_ENABLE_SME2=ON bash scripts/build_host_tools.sh all`
- Verification command: `bash scripts/verify_arm_paths.sh`
- Branch-metric equivalence command: `bash scripts/check_branch_metrics.sh`

The acquisition assembly verifier requires `smstart`/`smstop`, VGx4 ZA
`fmla`/`fmls`, and ZA transfer instructions in the SME2 object. On x86 or
non-SME2 builds, the portable checker reports `implementation = unavailable`.

See [docs/simd_status.md](docs/simd_status.md)
for the exact wording used in the codebase.
See [docs/benchmarking.md](docs/benchmarking.md)
for the exact benchmark scope and reporting notes.
See [docs/reproducibility.md](docs/reproducibility.md)
for the clean-checkout rerun procedure.
See [docs/technical_review.md](docs/technical_review.md)
for build-separation, numerical-equivalence, and instruction-evidence commands.

## What this repository does not claim

- It does not ship a live RTL-SDR capture path.
- It does not ship an Android replay app or end-to-end mobile replay path.
- It does not ship a mobile JNI bridge.
- It does not claim end-to-end SME2 Viterbi acceleration.
- It does not ship a mission-derived or Φsat-2 replay asset.
- It does not present local SME2 acquisition measurements as a general
  performance result or speedup.
- It does not claim cross-device performance reproducibility, thermal behavior,
  or a general NEON/SME2 speedup.

The repository includes a local timing harness so developers can measure their
own machine, but the README does not turn those local measurements into general
performance claims.

## Reproducibility

What works today:

- build and run the host-side canned replay flow
- run the reference timing/CFO acquisition search on five checked-in fixtures
- run the NEON acquisition path on native Arm64 builds and verify its numerical
  equivalence to the reference path
- compile and execute the SME2 acquisition path on supported SME2 hardware,
  verify every candidate correlation against the reference, and inspect the
  object for ZA VGx4 instructions
- run fixed acquisition workload classes with correctness-gated local timing,
  separate steady-state/per-capture/setup-inclusive modes, implementation
  workspace accounting, and JSON/CSV reporting
- regenerate the synthetic IQ asset and its metadata
- verify fixture seeds and SHA-256 hashes against the tracked manifest
- compare healthy, impaired, ambiguous, CRC-failed, and noise-only trust cases
  on checked-in inputs
- compare `viterbi-reference`, `viterbi-neon`, and `viterbi-sme2` entrypoints
  on the same canned input and evaluation window
- verify that reference, NEON-or-fallback, and SME2-or-fallback branch-metric
  preparation produce identical metric arrays on deterministic inputs

Required build environment:

- a development machine with CMake 3.22+, a C++17-capable compiler, Python 3,
  and `jq`

Optional hardware:

- an Arm64 machine with NEON support if you want to execute the acquisition
  NEON kernel or the checked-in NEON branch-metric preparation path
- an Arm machine and compiler targeting SME2 if you want `viterbi-sme2` to
  compile and run the SME2/SME streaming-mode branch-metric kernel or the SME2
  acquisition kernel

Build modes:

- default portable: `bash scripts/build_host_tools.sh all`
- explicit NEON: `SATCOMFEC_ENABLE_NEON=ON bash scripts/build_host_tools.sh all`
- explicit SME2: `SATCOMFEC_ENABLE_SME2=ON bash scripts/build_host_tools.sh all`
- CMake SME2 equivalent:
  `cmake -S . -B build/sme2 -DSATCOMFEC_ENABLE_SME2=ON`

`SATCOMFEC_ENABLE_SME2=ON` fails at configure/build time with a clear message
when the compiler does not accept the SME2 target flag or ACLE streaming
attribute. On Darwin arm64, the build tries a native+sme2 target before generic
Armv9 SME2 targets. The default CI path does not require SME2 hardware.

What is synthetic:

- `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- `data/synthetic/acquisition/*.iq`
- `data/synthetic/acquisition/*.json`
- the deterministic QPSK preamble in `data/synthetic/acquisition/`
- the replay payload and waveform produced by `scripts/generate_synthetic_iq.py`
- the acquisition captures produced by
  `scripts/generate_acquisition_fixtures.py`

What is not included:

- a public notebook
- live satellite captures
- mission-derived replay data
- a checked-in Gradle wrapper or APK build workflow
- a supported Android app
- SME2-accelerated Viterbi add-compare-select or traceback
- a claim that checked-in acquisition timings generalize beyond the exact host,
  build, and run metadata recorded with them

## Expected output

The replay runner prints JSON similar to:

```json
{
  "ok": true,
  "decoder": "viterbi-neon",
  "decoded_text": "SATCOM DEMO OK",
  "crc_ok": true,
  "acquisition": {
    "requested_implementation": "reference",
    "selected_implementation": "reference",
    "acquisition_success": true,
    "detected_timing_offset": 192,
    "detected_cfo_hz": 250.0,
    "normalized_peak": 0.996778,
    "normalized_peak_separation": 0.955311,
    "confidence": 0.953771,
    "confidence_calibrated": false,
    "ground_truth": {
      "timing_error_samples": 0,
      "cfo_hypothesis_error_hz": 0.0
    }
  },
  "trust_assessment": {
    "band": "high-confidence",
    "ambiguous_acquisition": false,
    "acquisition_rejected": false,
    "crc_not_evaluated": false,
    "crc_failed": false
  },
  "trust_score": 0.988191,
  "error": ""
}
```

The trust comparison distinguishes a clear healthy peak, a weaker impaired
case, a close competing peak, a post-acquisition CRC failure, and a noise-only
capture rejected before demodulation. Acquisition confidence is an uncalibrated
diagnostic, not a probability.

The acquisition benchmark reports fixed workload dimensions, the deterministic
seed and execution order, host/compiler/source-flag metadata, runtime Arm
feature detection, candidate and score equivalence, timing distributions,
throughput, relative median latency, and plan/workspace/output bytes. The
per-capture mode includes SME2 packing for each newly supplied IQ window while
reusing the acquisition plan and allocations. An implementation is timed only
after it matches the scalar result. See
[docs/benchmarking.md](docs/benchmarking.md) for the exact four workload
classes, fairness contract, and statistical method.

The legacy decoder harness prints its assumptions inline. Those assumptions
are:

- same canned IQ file for all paths
- same samples-per-symbol setting
- same framed coded-bit window
- same scalar decoder state machine and traceback logic
- branch-metric preparation timed separately from full decode
- full decode timing includes branch-metric preparation plus scalar Viterbi
  recurrence and traceback
- same timed iteration count inside one process
- small replay frames may be dominated by SME2 setup and streaming-mode overhead

The legacy decoder report also includes:

- compile target
- implementation class and summary for each path
- selected branch-metric implementation for each path
- branch-metric preparation time for each path
- full decode time for each path
- prepared-frame metadata and checksum
- decoded-bit counts and decoded-bit checksums for all paths

These fields are there to make the comparison auditable, not to imply a
portable performance claim.

## Repository map

Key paths:

- `src/`
  Host-side acquisition, replay, decoder, DSP, trust, and utility modules
- `data/synthetic/acquisition/`
  Deterministic preamble, acquisition captures, and ground-truth metadata
- `data/synthetic/canned_replay/`
  Checked-in preamble plus healthy, impaired, ambiguous, CRC-failed, and
  noise-only replay fixtures and metadata
- `scripts/`
  Supported host-side build, acquisition, replay, trust, and validation
  entrypoints
- `benchmarks/results/`
  Tracked raw local benchmark reports and their independent-run summaries
- `Makefile`
  Thin top-level command surface for the supported host-side workflow
- `tools/`
  Host-side CLI binaries used by the shell wrappers
- `tests/`
  Python regression tests for acquisition, replay correctness, trust behavior,
  and decoder alignment
- `docs/`
  Scope, architecture, trust, benchmarking, and reproducibility notes

## Script inventory

Supported host-side scripts:

- `make help`
- `scripts/build_host_tools.sh`
- `scripts/run_acquisition_demo.sh`
- `scripts/check_acquisition_neon.sh`
- `scripts/verify_acquisition_neon.sh`
- `scripts/check_sme2_acquisition.sh`
- `scripts/verify_sme2_acquisition_assembly.sh`
- `scripts/run_replay_demo.sh`
- `scripts/check_acquisition_demo.sh`
- `scripts/check_replay_demo.sh`
- `scripts/compare_trust_cases.sh`
- `scripts/validate_decoder_alignment.sh`
- `scripts/check_branch_metrics.sh`
- `scripts/verify_arm_paths.sh`
- `scripts/benchmark_acquisition.sh`
- `scripts/repeat_acquisition_benchmark.py`
- `scripts/benchmark_decoder_paths.sh`
- `scripts/generate_synthetic_iq.py`
- `scripts/generate_acquisition_fixtures.py`
- `scripts/check_compile_commands.py`
- `scripts/update_fixture_checksums.py`
- `scripts/verify_public_workflow.sh`

Automated validation:

- `tests/test_host_replay.py`
- `tests/test_acquisition.py`
- `tests/test_build_integrity.py`
- `.github/workflows/host-replay.yml`

## Repository layout

```text
satcom-fec-trust-lab/
├─ .github/workflows/host-replay.yml # Clean-checkout host CI
├─ CMakeLists.txt                    # Authoritative host/NDK build graph
├─ README.md
├─ assets/social/                   # Repository artwork
├─ benchmarks/results/              # Tracked raw local benchmark evidence
├─ data/synthetic/acquisition/      # Acquisition IQ and ground truth
├─ data/synthetic/canned_replay/    # Replay IQ and metadata
├─ docs/                            # Short notes for the public demo
├─ scripts/                         # Host build, replay, and validation scripts
├─ src/                             # Acquisition, replay, FEC, DSP, and trust code
├─ tests/                           # Automated host workflow checks
└─ tools/                           # Host-side CLI sources
```

## License

MIT. See [LICENSE](LICENSE).
