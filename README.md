# Satcom FEC Trust Lab

Host-side C++17 replay and acquisition tools for a deterministic synthetic
satellite-link experiment. The supported replay path performs IQ front-end
normalization, timing/CFO acquisition, aligned BPSK demodulation, convolutional
decode, CRC-8 validation, and explainable trust diagnostics.

IQ-domain acquisition is the architecture-optimization workload. It has a
scalar correctness oracle, a genuine Arm NEON kernel, and an opt-in genuine
SME2 kernel using ZA VGx4 accumulation. Requested accelerated paths report
`unavailable` when they cannot execute; they never run scalar code under an
accelerated label.

This is not a live receiver, an Android application, or a full accelerated FEC
stack. All checked-in captures are deterministic synthetic fixtures.

## Status

Implemented and supported:

- end-to-end host replay from checked-in complex float32 IQ to payload and CRC;
- IQ preamble acquisition over timing and CFO hypothesis grids;
- scalar, NEON, and SME2 acquisition implementations with equivalence checks;
- healthy, impaired, ambiguous, CRC-failed, and no-signal replay cases;
- structured JSON trust diagnostics;
- fixed acquisition workloads and three benchmark timing contracts;
- CMake builds, CTests, Python regressions, strict warnings, and sanitizers;
- a native Android NDK acquisition benchmark executable for ADB, without an
  APK or UI.

Intentionally limited:

- the waveform and thresholds are demo-specific, not mission-derived;
- acquisition is a finite preamble search, not a tracking loop;
- trust scores are deterministic engineering heuristics, not calibrated
  probabilities;
- Viterbi add-compare-select and traceback remain scalar;
- no public NEON or SME2 LDPC implementation exists;
- local benchmark reports are device observations, not portable speedup claims.

## Quick start

Requirements: CMake 3.22+, a C++17 compiler, Python 3, Bash, Make, and `jq`.

```sh
make build
make replay
make compare-trust
make check-acquisition
make benchmark-acquisition
```

Run the complete clean-checkout correctness workflow:

```sh
make verify
```

`make verify` checks fixture hashes, strict compile warnings, per-source target
flag isolation, CTests, replay/acquisition/trust/FEC validation, Python tests,
and portable ASan/UBSan tests. Architecture-specific execution is attempted
only when the host and compiler support it.

## Replay examples

Baseline IQ-to-payload replay:

```sh
bash scripts/run_replay_demo.sh
```

The JSON includes the actual acquisition and decoder implementations, timing
and CFO estimates, top-two acquisition scores, downstream frame-sync evidence,
CRC state, trust components, and synthetic ground truth. A successful baseline
contains:

```json
{
  "ok": true,
  "decoder": "viterbi-reference",
  "decoded_text": "SATCOM DEMO OK",
  "crc_ok": true,
  "acquisition": {
    "selected_implementation": "reference",
    "detected_timing_offset": 192,
    "detected_cfo_hz": 250.0,
    "acquisition_success": true
  },
  "trust_assessment": {
    "band": "high-confidence",
    "weak_soft_decisions": false,
    "ambiguous_acquisition": false,
    "ambiguous_frame_sync": false
  }
}
```

Compare deterministic trust states:

```sh
bash scripts/compare_trust_cases.sh
```

The command verifies the expected ordering across healthy, impaired,
ambiguous, CRC-failed, and no-signal inputs. The no-signal case is rejected at
acquisition before demodulation; the failed case acquires a frame but rejects
it after CRC.

## Acquisition paths

All acquisition implementations evaluate the same known complex preamble over
the same timing/CFO candidate grid and rank candidates by correlation magnitude
squared.

- `reference`: scalar float64 correctness oracle in
  `src/acquisition/acquisition_reference.cpp`.
- `neon`: float32 complex multiply-accumulate in
  `src/acquisition/acquisition_neon.cpp`, built with source-specific NEON flags.
- `sme2`: float32 ZA-backed VGx4 `FMLA`/`FMLS` accumulation in
  `src/acquisition/acquisition_sme2.cpp`, built only with SME2 ACLE support and
  entered only after runtime feature detection.

Run a fixture explicitly:

```sh
bash scripts/run_acquisition_demo.sh \
  data/synthetic/acquisition/clean.iq \
  data/synthetic/acquisition/clean.json \
  reference
```

On a supported native SME2 machine:

```sh
SATCOMFEC_ENABLE_SME2=ON bash scripts/build_host_tools.sh all
bash scripts/check_sme2_acquisition.sh --require-sme2
bash scripts/verify_sme2_acquisition_assembly.sh
```

The assembly verifier requires streaming boundaries, ZA transfers, and SME2
VGx4 multiply-accumulate instructions. Macro detection alone is not accepted.
See `docs/sme2_acquisition.md` for the kernel design.

## FEC paths

The replay defaults to `viterbi-reference`: scalar branch-metric preparation,
add-compare-select, and traceback. `viterbi-neon` is an optional partial path
that accelerates branch-metric preparation only.

A historical locally streaming SVE-style branch-metric experiment is preserved
under `experiments/viterbi_branch_metrics/`. It is gated by an SME2-capable
build because that is how the original experiment enters streaming mode, but
it does not use ZA or an SME2-specific multi-vector operation. Its explicit
`viterbi-streaming-vector` experiment executable is excluded from the default
build target and the path is not exposed by the supported replay CLI. It is not
evidence of SME2 Viterbi acceleration.

```sh
bash scripts/check_branch_metrics.sh
bash experiments/viterbi_branch_metrics/run.sh
```

The convolutional decoder remains a functional downstream replay component.
The small bit-flip LDPC decoder is a simplified reference utility; no
accelerated LDPC path is claimed.

## Benchmarking

The supported performance workload is IQ-domain acquisition:

```sh
bash scripts/benchmark_acquisition.sh \
  --json build/acquisition-benchmark.json \
  --csv build/acquisition-benchmark.csv
```

The fixed `small`, `medium`, `large`, and `very-large` workloads are defined in
source before timing. Every available implementation is correctness-gated.
Reports include raw samples, execution order, source-specific compile flags,
host/runtime metadata, workspace bytes, and results for:

- `steady-state`: precomputed plan and prepared implementation workspace;
- `per-capture`: reusable plan, with work required for each new IQ window;
- `setup-inclusive`: plan generation, allocation, and execution.

Five independent process reports can be retained with:

```sh
python3 scripts/repeat_acquisition_benchmark.py \
  --output-dir build/acquisition-repeatability
```

Tracked local reports are under `benchmarks/results/`. Read hardware, compiler,
commit, dirty-tree state, and raw timing from each JSON report. The repository
does not claim a general NEON or SME2 speedup, thermal result, energy result, or
cross-device result. See `docs/benchmarking.md` for the fairness contract.

## Build isolation

CMake is authoritative. Reference, NEON, and SME2 acquisition code lives in
separate translation units. Architecture flags and compiled-kernel definitions
are source-specific; the scalar reference is also built with loop and SLP
auto-vectorization disabled where supported.

```sh
cmake -S . -B build/portable
cmake --build build/portable
ctest --test-dir build/portable --output-on-failure
python3 scripts/check_compile_commands.py \
  --build-dir build/portable --expect portable
```

For Arm-specific checks:

```sh
bash scripts/verify_arm_paths.sh
bash scripts/verify_acquisition_neon.sh
```

An explicit unsupported accelerated build fails with a clear configuration or
availability message; the portable x86 build does not require Arm hardware.

## Android ADB benchmark

The Android surface is a command-line acquisition benchmark only. It uses the
NDK CMake toolchain, pushes one executable with ADB, runtime-gates SME2 through
Linux/Android capability bits, and emits the same benchmark JSON schema. It
does not include an APK, JNI bridge, Kotlin, Compose, or replay UI.

```sh
bash scripts/verify_android_benchmark_build.sh --sme2 auto
bash scripts/run_android_benchmark.sh --sme2 auto -- --workload small
```

An actual device is required for runtime measurements. See
`docs/android_benchmark.md` for prerequisites and safety details.

## Repository map

- `src/acquisition/`: reference, NEON, SME2, planning, and dispatch code
- `src/demo/`: supported replay orchestration and acquisition integration
- `src/dsp/`: normalization, BPSK soft decisions, and secondary frame sync
- `src/fec/`: convolutional/FEC functionality used by replay
- `src/trust/`: inspectable trust features, score, and status flags
- `tools/`: supported host CLIs and correctness executables
- `experiments/`: retained non-primary experiments with explicit scope
- `data/synthetic/`: deterministic IQ fixtures and ground-truth sidecars
- `tests/`: Python regressions and structured golden-output subsets
- `benchmarks/results/`: tracked local acquisition benchmark reports
- `docs/`: design, methodology, reproducibility, and review guides

## Documentation

- `docs/acquisition_design.md`: acquisition mathematics and fixture criteria
- `docs/sme2_acquisition.md`: genuine SME2 data blocking and ZA mechanism
- `docs/benchmarking.md`: fixed workloads, timing contracts, and limitations
- `docs/trust_monitors.md`: trust inputs, weights, and non-calibration limits
- `docs/reproducibility.md`: complete validation workflow and fixture hashes
- `docs/technical_review.md`: equivalence, assembly, and build-isolation checks
- `docs/architecture.md`: end-to-end pipeline and build boundaries
- `docs/simd_status.md`: exact implementation maturity by path

## License

See `LICENSE`.
