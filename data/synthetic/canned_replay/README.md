# Canned Replay Asset

This folder contains the supported public demo input:

- `demo_conv_bpsk.iq`: baseline interleaved float32 IQ samples
- `demo_conv_bpsk.json`: baseline metadata used by validation and tests
- `demo_conv_bpsk_impaired.iq`: impaired interleaved float32 IQ samples
- `demo_conv_bpsk_impaired.json`: impaired metadata used by validation and tests
- `demo_conv_bpsk_failed.iq`: failed interleaved float32 IQ samples
- `demo_conv_bpsk_failed.json`: failed-case metadata used by validation and tests

All scenarios are synthetic and deterministic. They carry the ASCII payload
`SATCOM DEMO OK`, append a CRC-8 byte, convolutionally encode the payload, add
a 16-bit sync word, and modulate the result with oversampled BPSK. The impaired
fixture adds deterministic noise, stronger amplitude ripple, and a short fade
while remaining decodable by the supported replay path. The failed fixture
preserves sync acquisition but corrupts part of the coded-data region so the
same replay path terminates with CRC failure.

Regenerate the asset with:

```bash
python3 scripts/generate_synthetic_iq.py
```
