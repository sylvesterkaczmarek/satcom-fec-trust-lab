# Reproducibility

This repository is intentionally small enough to be rerun from a clean checkout
without extra data downloads or hidden setup.

Publicly supported workflow:

1. Build the host-side tools.
2. Run and verify the scalar acquisition fixtures.
3. Run the baseline replay fixture.
4. Run the impaired and failed fixtures to inspect trust degradation and CRC rejection.
5. Verify the replay output, trust comparison, and decoder-path alignment.

Exact commands:

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
make align
make check-metrics
make verify-arm
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
bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq
bash scripts/check_replay_demo.sh
bash scripts/compare_trust_cases.sh
bash scripts/validate_decoder_alignment.sh
bash scripts/check_branch_metrics.sh
bash scripts/verify_arm_paths.sh
python3 -m unittest discover -s tests -v
```

Checked-in fixtures:

- `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`
- `data/synthetic/acquisition/preamble_qpsk_256.iq`
- `data/synthetic/acquisition/{clean,noisy,frequency_offset,ambiguous,weak_faded}.iq`
- matching JSON ground-truth sidecars for each acquisition capture

Generated from source in this repo:

- `scripts/generate_synthetic_iq.py`
- `scripts/generate_acquisition_fixtures.py`

What CI verifies:

- baseline replay decode correctness
- scalar acquisition timing/CFO recovery on five deterministic fixtures
- reference/NEON acquisition equivalence on native Arm64 CI
- explicit NEON-unavailable reporting on portable x86 CI
- explicit SME2-unavailable reporting on portable and Arm64 CI without SME2
- direct NEON kernel equivalence over vector-width, tail, weak-signal, and
  high-amplitude cases
- local SME2 kernel equivalence over preamble, timing-grid, CFO-grid, and
  scalable-vector tail cases when SME2 execution is available
- deterministic byte-for-byte acquisition fixture regeneration
- healthy versus impaired versus failed trust comparison
- reference versus partial-NEON versus SME2-or-fallback path alignment on the
  same prepared replay frame
- reference, NEON-or-fallback, and SME2-or-fallback branch-metric equivalence on
  deterministic short and full-frame-sized inputs
- Python host-side regression tests
- golden structured-output subsets for replay, trust comparison, and benchmark reports

What reproducibility does not mean here:

- no claim of device-level performance reproducibility
- no claim of end-to-end SME2 Viterbi acceleration
- no live RF capture path
- no mission-derived waveform fidelity claim
