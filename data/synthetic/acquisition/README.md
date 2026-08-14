# Acquisition fixtures

`generate_acquisition_fixtures.py` creates the binary IQ captures and JSON
metadata in this directory. The binary format is interleaved little-endian
float32 I/Q.

All fixtures use the same deterministic 256-sample QPSK preamble and a
4,096-sample receive window. The metadata records the generator seed, signal
parameters, timing search range, CFO hypotheses, and ground truth.

Regenerate the checked-in files from the repository root:

```sh
python3 scripts/generate_acquisition_fixtures.py
```
