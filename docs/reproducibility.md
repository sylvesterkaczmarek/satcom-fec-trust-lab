# Reproducibility

This repository is intentionally small enough to be rerun from a clean checkout
without extra data downloads or hidden setup.

Publicly supported workflow:

1. Build the host-side tools.
2. Run the baseline replay fixture.
3. Run the impaired and failed fixtures to inspect trust degradation and CRC rejection.
4. Verify the replay output, trust comparison, and decoder-path alignment.

Exact commands:

```bash
make build
make replay
make replay-impaired
make replay-failed
make check
make compare-trust
make align
make test
```

Equivalent script-by-script flow:

```bash
bash scripts/build_host_tools.sh all
bash scripts/run_replay_demo.sh
bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq
bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq
bash scripts/check_replay_demo.sh
bash scripts/compare_trust_cases.sh
bash scripts/validate_decoder_alignment.sh
python3 -m unittest discover -s tests -v
```

Checked-in fixtures:

- `data/synthetic/canned_replay/demo_conv_bpsk.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_impaired.json`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.iq`
- `data/synthetic/canned_replay/demo_conv_bpsk_failed.json`

Generated from source in this repo:

- `scripts/generate_synthetic_iq.py`

What CI verifies:

- baseline replay decode correctness
- healthy versus impaired versus failed trust comparison
- NEON versus simplified-SME2 path alignment on the same prepared replay frame
- Python host-side regression tests
- golden structured-output subsets for replay, trust comparison, and benchmark reports

What reproducibility does not mean here:

- no claim of device-level performance reproducibility
- no claim of SME2 acceleration
- no live RF capture path
- no mission-derived waveform fidelity claim
