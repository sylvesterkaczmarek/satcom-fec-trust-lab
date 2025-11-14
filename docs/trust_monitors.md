# Trust monitors

The trust layer starts simple and can grow over time.

## Current stub

Right now, the stub implementation computes:

- Mean LLR over a window of soft bits  
- A supplied windowed frame error rate  
- A scalar score that increases when mean LLR magnitude is high and FER is low

This is intentionally basic so the rest of the pipeline can be wired up.

## Planned monitors

Future versions intend to add:

- Spectral kurtosis based indicators for interference  
- Preamble correlation strength and timing jitter  
- Error vector magnitude and carrier frequency offset trends  
- LDPC syndrome statistics and iteration counts  
- Change detectors over FER and other features

The key idea is that the trust layer can abstain when the feature pattern falls outside calibrated ranges, instead of always returning a confident answer.
