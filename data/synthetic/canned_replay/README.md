# Canned Replay Asset

This folder contains the supported public demo input:

- `demo_conv_bpsk.iq`: interleaved float32 IQ samples
- `demo_conv_bpsk.json`: frame metadata for the replay runner

The frame is synthetic and deterministic. It carries the ASCII payload
`SATCOM DEMO OK`, appends a CRC-8 byte, convolutionally encodes the payload,
adds a 16-bit sync word, and modulates the result with oversampled BPSK.

Regenerate the asset with:

```bash
python3 scripts/generate_synthetic_iq.py
```
