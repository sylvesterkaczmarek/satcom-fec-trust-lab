# Satcom FEC Trust Lab

![Satcom FEC Trust Lab](assets/social/github-social-card-satcom-fec-trust-lab.png)

This repository is a scoped host-side satcom replay and decoder-comparison
project built around deterministic synthetic IQ fixtures. The supported public
workflow loads a checked-in IQ file, performs front-end normalization,
demodulates oversampled BPSK, locates a sync word, decodes a convolutionally
coded frame with Viterbi, checks a CRC-8 byte, and reports structured trust
diagnostics alongside the decoded payload.

The public repo provides a scoped host-side replay demo, not a supported
Android app or full SME2-optimized mobile decoder.

## Repo status

- Publication-safe today: the host-side canned replay flow
- Included validation: host-side automated tests and host CI
- Not included: Android app packaging, JNI/mobile replay wiring,
  SME2-specific decoder kernels, live RF capture, benchmark claims, or thermal
  claims

## Supported scope

- One host-side replay path from checked-in IQ to decoded payload plus trust
  output
- One local decoder-path timing comparison between `viterbi-neon` and
  `viterbi-sme2`
- One healthy versus impaired versus failed trust comparison using checked-in
  synthetic fixtures

## What is included

- A checked-in synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- A checked-in impaired synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- A checked-in CRC-failing synthetic IQ recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- Metadata for that recording at
  `data/synthetic/canned_replay/demo_conv_bpsk.json`
- Metadata for the impaired recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- Metadata for the failed recording at
  `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- Purpose-built host-side sources under `src/`
- A top-level `CMakeLists.txt` for host builds
- A host-side runner at `scripts/run_replay_demo.sh`
- A host-side build helper at `scripts/build_host_tools.sh`
- A verification script at `scripts/check_replay_demo.sh`
- A trust comparison script at `scripts/compare_trust_cases.sh`
- A decoder alignment check at `scripts/validate_decoder_alignment.sh`
- A local benchmark harness at `scripts/benchmark_decoder_paths.sh`
- Automated tests at `tests/test_host_replay.py`
- Golden structured-output subsets under `tests/golden/`
- Host CI at `.github/workflows/host-replay.yml`
- Native C++ modules for front-end processing, framing, Viterbi decode, a small
  LDPC-style bit-flip decoder, and trust scoring

## What the supported demo does

The canned replay frame carries the ASCII payload `SATCOM DEMO OK`. The
generator script appends a CRC-8 byte, convolutionally encodes the payload with
a rate-1/2 code using generators `(7, 5)` in octal, prepends a 16-bit sync
word, and modulates the result as oversampled BPSK in interleaved float32 IQ.

The replay runner reports:

- decoded text
- CRC result
- front-end normalization statistics
- demodulation and framing statistics
- trust features, trust-score breakdown, and a trust assessment band

The checked-in synthetic asset set contains three scenarios:

- `healthy`: the baseline replay used by the quick start
- `impaired`: a replay with deterministic added noise, stronger amplitude
  ripple, and a short mid-frame fade
- `failed`: a replay that still acquires sync but has a deliberately corrupted
  coded-data segment, so the supported replay path reaches CRC rejection

## Quick start

Requirements for the supported quick start:

- `bash`
- `make`
- `c++` with C++17 support
- `python3`
- `jq` for the validation scripts

Recommended first run:

```bash
make build
make replay
make check
```

Equivalent direct commands:

```bash
bash scripts/build_host_tools.sh all
bash scripts/run_replay_demo.sh
bash scripts/check_replay_demo.sh
```

Common follow-on commands:

```bash
make replay-impaired
make replay-failed
make compare-trust
make align
make benchmark
make test
```

Regenerate the checked-in synthetic fixtures:

```bash
make regenerate
```

Run the alternate decoder entrypoint:

```bash
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk.iq viterbi-sme2
```

The benchmark is intentionally narrow. It compares the checked-in
`viterbi-neon` and `viterbi-sme2` entrypoints on the same prepared replay frame
inside one process and reports local timing plus decoded-bit alignment data. It
does not claim a general NEON or SME2 speed result.

The supported quick start does not use Gradle. No Gradle wrapper or Android
build entrypoint is included in this publication-scoped revision.

## Example sessions

Healthy replay:

```bash
bash scripts/run_replay_demo.sh | jq '{decoder, decoded_text, crc_ok, trust_score, trust_assessment}'
```

Example output:

```json
{
  "decoder": "viterbi-neon",
  "decoded_text": "SATCOM DEMO OK",
  "crc_ok": true,
  "trust_score": 1,
  "trust_assessment": {
    "band": "high-confidence",
    "weak_soft_bits": false,
    "ambiguous_sync": false,
    "demod_clipping": false,
    "crc_failed": false
  }
}
```

Healthy versus impaired versus failed trust comparison:

```bash
bash scripts/compare_trust_cases.sh
```

Example output:

```json
{
  "healthy": {
    "trust_score": 1.0,
    "trust_band": "high-confidence",
    "weak_llr_fraction": 0.0
  },
  "impaired": {
    "trust_score": 0.93645,
    "trust_band": "guarded",
    "weak_llr_fraction": 0.147541
  },
  "failed": {
    "trust_score": 0.35,
    "trust_band": "low-confidence",
    "error": "CRC mismatch"
  },
  "comparison": {
    "healthy_impaired_same_payload": true,
    "trust_score_order_ok": true,
    "failed_crc_rejected": true
  }
}
```

## SIMD implementation status

- `viterbi-neon`: real implementation on Arm NEON targets. The checked-in NEON
  work is branch-metric preparation; add-compare-select and traceback remain
  shared with the reference decode core.
- `viterbi-sme2`: simplified implementation. It uses the same scalar decode
  core and does not include an SME2-specific kernel.
- `ldpc-neon` and `ldpc-sme2`: simplified implementations. Both use the same
  bit-flip reference decoder and are not benchmark targets in this repository.

See [docs/simd_status.md](docs/simd_status.md)
for the exact wording used in the codebase.
See [docs/benchmarking.md](docs/benchmarking.md)
for the exact benchmark scope and reporting notes.
See [docs/reproducibility.md](docs/reproducibility.md)
for the clean-checkout rerun procedure.

## What this repository does not claim

- It does not ship a live RTL-SDR capture path.
- It does not ship an Android replay app path.
- It does not ship a mobile JNI bridge or on-device replay workflow.
- It does not claim an SME2 acceleration result.
- It does not ship a mission-derived or Φsat-2 replay asset.
- It does not claim benchmark reproducibility, thermal behavior, or cross-device
  performance conclusions.

The repository includes a local timing harness so developers can measure their
own machine, but the README does not turn those local measurements into general
performance claims.

## Reproducibility

What works today:

- build and run the host-side canned replay flow
- regenerate the synthetic IQ asset and its metadata
- compare healthy, impaired, and failed trust-monitoring cases on checked-in
  inputs
- compare the `viterbi-neon` and `viterbi-sme2` entrypoints on the same canned
  input and evaluation window

Required hardware:

- a development machine with a C++17-capable `c++` compiler

Optional hardware:

- an Arm64 machine with NEON support if you want the checked-in NEON kernel to
  execute as NEON rather than the portable fallback

What is synthetic:

- `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- the replay payload and waveform produced by `scripts/generate_synthetic_iq.py`

What is not included:

- a public notebook
- live satellite captures
- mission-derived replay data
- a checked-in Gradle wrapper or Android build workflow
- a supported Android app
- an SME2-specific kernel

## Expected output

The replay runner prints JSON similar to:

```json
{
  "ok": true,
  "iq_path": "data/synthetic/canned_replay/demo_conv_bpsk.iq",
  "decoder": "viterbi-neon",
  "implementation_class": "real",
  "implementation_summary": "Real implementation on Arm NEON targets: NEON precomputes branch metrics; add-compare-select and traceback remain shared scalar reference code.",
  "samples_per_symbol": 8,
  "frame_soft_bits": 244,
  "expected_payload_bytes": 14,
  "decoded_payload_bytes": 14,
  "decoded_text": "SATCOM DEMO OK",
  "crc_ok": true,
  "front_end": {
    "sample_count": 2080,
    "dc_i": 0.070320,
    "dc_q": -0.019993,
    "rms_before_normalization": 1.001070,
    "rms_after_normalization": 1.0
  },
  "demod": {
    "symbol_count": 260,
    "samples_per_symbol": 8,
    "max_abs_symbol_mean": 1.116615,
    "clipped_symbol_count": 0
  },
  "framing": {
    "sync_start_index": 0,
    "frame_start_index": 16,
    "frame_length": 244,
    "sync_score": 16,
    "has_second_best_correlation": false,
    "second_best_sync_start_index": 0,
    "second_best_sync_score": 16
  },
  "trust_features": {
    "mean_abs_llr": 113.213,
    "normalized_mean_abs_llr": 1.0,
    "weak_llr_fraction": 0.0,
    "normalized_sync_score": 1.0,
    "normalized_sync_margin": 1.0,
    "clipped_symbol_fraction": 0.0,
    "crc_pass": 1.0
  },
  "trust_breakdown": {
    "llr_strength": 1.0,
    "llr_consistency": 1.0,
    "sync_quality": 1.0,
    "sync_margin_quality": 1.0,
    "demod_quality": 1.0,
    "crc_quality": 1.0,
    "capped_by_crc_failure": false,
    "score": 1.0
  },
  "trust_assessment": {
    "band": "high-confidence",
    "weak_soft_bits": false,
    "ambiguous_sync": false,
    "demod_clipping": false,
    "crc_failed": false
  },
  "trust_score": 1.0,
  "error": ""
}
```

The trust comparison script prints a compact healthy versus impaired versus
failed summary. In the checked-in deterministic asset set, the healthy replay decodes with
`high-confidence`, the impaired replay still decodes but drops to `guarded`
because its soft decisions are weaker, and the failed replay is capped at
`low-confidence` because CRC rejection is treated as a hard trust limiter.

The benchmark harness prints its assumptions inline. In the current repository
those assumptions are:

- same canned IQ file for both paths
- same samples-per-symbol setting
- same framed coded-bit window
- same decoder state machine and traceback logic
- same timed iteration count inside one process

The benchmark report also includes:

- compile target
- implementation class and summary for each path
- prepared-frame metadata and checksum
- decoded-bit counts and decoded-bit checksums for both paths

These fields are there to make the comparison auditable, not to imply a
portable performance claim.

## Repository map

Key paths:

- `src/`
  Purpose-built host-side replay pipeline, decoder wrappers, trust logic, and
  utilities
- `data/synthetic/canned_replay/`
  Checked-in healthy, impaired, and failed replay fixtures plus metadata
- `scripts/`
  Supported host-side build, replay, trust, and validation entrypoints
- `Makefile`
  Thin top-level command surface for the supported host-side workflow
- `tools/`
  Host-side CLI binaries used by the shell wrappers
- `tests/`
  Python regression tests for replay correctness, trust behavior, and decoder
  alignment
- `docs/`
  Scope, architecture, trust, benchmarking, and reproducibility notes

## Script inventory

Supported host-side scripts:

- `make help`
- `scripts/build_host_tools.sh`
- `scripts/run_replay_demo.sh`
- `scripts/check_replay_demo.sh`
- `scripts/compare_trust_cases.sh`
- `scripts/validate_decoder_alignment.sh`
- `scripts/benchmark_decoder_paths.sh`
- `scripts/generate_synthetic_iq.py`

Automated validation:

- `tests/test_host_replay.py`
- `.github/workflows/host-replay.yml`

## Repository layout

```text
satcom-fec-trust-lab/
├─ .github/workflows/host-replay.yml # Clean-checkout host CI
├─ CMakeLists.txt                    # Host-side CMake entrypoint
├─ README.md
├─ assets/social/                   # Repository artwork
├─ data/synthetic/canned_replay/    # Checked-in demo IQ and metadata
├─ docs/                            # Short notes for the public demo
├─ scripts/                         # Host build, replay, and validation scripts
├─ src/                             # Host-side replay, FEC, DSP, and trust code
├─ tests/                           # Automated host replay checks
└─ tools/                           # Host-side replay runner source
```

## License

MIT. See [LICENSE](LICENSE).
