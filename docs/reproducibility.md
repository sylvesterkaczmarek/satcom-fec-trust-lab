# Reproducibility

The public correctness path requires no external dataset. Complex-IQ fixtures,
ground-truth sidecars, seeds, generators, and SHA-256 values are checked in.

## Portable verification

Requirements: CMake 3.22+, a C++17 compiler, Python 3, Bash, Make, and `jq`.

```sh
make verify
```

This starts with fresh verification build directories and runs:

- fixture manifest and content-hash validation;
- `-Wall -Wextra -Wpedantic -Werror` portable compilation;
- compile-command checks for translation-unit target-flag isolation;
- CTests and Python regressions;
- replay, acquisition, trust, and downstream FEC validation;
- ASan/UBSan CTests;
- architecture probes supported by the current host/compiler.

On non-Arm hosts, accelerated checks report unavailable without claiming that
NEON or SME2 executed.

## Arm64 NEON verification

```sh
bash scripts/check_acquisition_neon.sh --require-neon
bash scripts/verify_acquisition_neon.sh
```

The equivalence check requires actual NEON execution. The verifier inspects the
NEON and scalar objects independently.

## SME2 verification

```sh
bash scripts/check_sme2_acquisition.sh --require-sme2
bash scripts/verify_sme2_acquisition_assembly.sh
```

These commands require native SME2 execution and SME2-specific object-code
evidence. Unsupported hardware is reported as unavailable; no fallback is
labeled SME2.

## Android cross-build verification

With an Android NDK available:

```sh
bash scripts/verify_android_benchmark_build.sh --sme2 auto
```

This validates the AArch64 command-line executable and target-specific object
code without claiming device execution. Running the benchmark requires a
separately authorized ADB device; no Android runtime result is tracked today.

## Fixture provenance

Replay fixtures:

- `demo_conv_bpsk`: healthy decode;
- `demo_conv_bpsk_impaired`: weaker but decodable;
- `demo_conv_bpsk_ambiguous`: competing acquisition peak;
- `demo_conv_bpsk_failed`: acquisition succeeds, CRC fails;
- `demo_conv_bpsk_no_signal`: acquisition rejection before demodulation.

Standalone acquisition fixtures:

- `clean`;
- `noisy`;
- `frequency_offset`;
- `ambiguous`;
- `weak_faded`.

Every sidecar records generator identity, deterministic seeds, IQ SHA-256, and
preamble SHA-256. `data/synthetic/fixture_manifest.json` records byte lengths
and hashes for all generated files. The generator sources are:

- `scripts/generate_synthetic_iq.py`;
- `scripts/generate_acquisition_fixtures.py`;
- `scripts/update_fixture_checksums.py`.

Ground truth is read for reporting and tests after acquisition. It is not used
to choose the detected candidate.

## Benchmark reproducibility

The benchmark fixes workload definitions in source, correctness-gates every
implementation, randomizes timed implementation order with a reported seed,
retains raw timing samples, and records host/compiler/build metadata.

Tracked timing evidence is versioned by benchmarked source commit under
`benchmarks/results/`. `benchmarks/results/b6ed1ec/` contains five independent
clean-tree processes for current-kernel source commit
`b6ed1ec073ea4406255c3201a39e81ecc67e21fd`. Each raw report records Apple M5
Pro (`Mac17,9`), Apple Clang 21.0.0, all three timing modes, fixed workload
definitions, correctness, raw samples, and memory accounting.

`benchmarks/results/a83cd53/` remains historical evidence for source commit
`a83cd53ffe153fa69329194174f735d0a972380d`; it describes the earlier packed
SME2 input path and earlier NEON baseline only. The raw JSON is authoritative
for that source commit.

A local smoke measurement can be generated with:

```sh
bash scripts/benchmark_acquisition.sh \
  --workload small \
  --warmup-rounds 1 \
  --samples 3 \
  --min-sample-ms 5 \
  --json build/acquisition-smoke.json
```

New local output describes only the current host. It does not reproduce the
tracked Apple host values unless hardware, source commit, compiler, flags,
and runtime conditions match.

## CI evidence

The workflow checks:

- portable Linux build, tests, replay, trust, acquisition, and sanitizers;
- native Arm64/Linux NEON execution and numerical equivalence;
- NEON instruction evidence and scalar-reference isolation;
- Android NDK SME2 compilation and ZA VGx4 instruction evidence without SME2
  execution;
- fixture regeneration and hash stability;
- benchmark schema, correctness gating, timing contracts, and memory fields.

CI timing is a smoke check, not publication performance evidence.

## Limits

Reproducibility here does not mean:

- live RF or mission-waveform reproduction;
- device-independent performance;
- controlled CPU frequency, affinity, or thermal state;
- energy or power measurement;
- calibrated acquisition or trust probabilities;
- full Viterbi or LDPC SME2 acceleration.
