# Canned replay fixtures

This directory contains deterministic float32 complex-IQ inputs for the
supported end-to-end replay path. Each 4,096-sample capture uses the checked-in
`preamble_qpsk_256.iq` acquisition sequence and a JSON sidecar with generation
parameters and ground truth.

Signal-bearing captures contain leading noise, the 256-sample QPSK preamble,
and an oversampled BPSK frame. The frame carries `SATCOM DEMO OK`, a CRC-8 byte,
and a rate-1/2 `(7, 5)` convolutional code behind a 16-bit secondary sync word.

- `demo_conv_bpsk`: healthy decode with non-zero timing and CFO
- `demo_conv_bpsk_impaired`: weaker acquisition peak and degraded frame
- `demo_conv_bpsk_ambiguous`: correct signal plus a competing preamble peak
- `demo_conv_bpsk_failed`: successful acquisition followed by CRC rejection
- `demo_conv_bpsk_no_signal`: noise-only acquisition rejection

The metadata is used only to report and test ground-truth error. The replay
pipeline does not use ground truth to select timing or CFO.

Regenerate every file with:

```bash
python3 scripts/generate_synthetic_iq.py
```
