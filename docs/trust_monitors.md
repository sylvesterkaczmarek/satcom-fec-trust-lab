# Trust Monitors

The replay demo computes a small set of explicit trust features from the
supported host-side flow:

- mean absolute LLR over the framed coded bits
- normalized mean absolute LLR
- weak-LLR fraction, defined as the fraction of framed soft bits with
  `|LLR| < 48`
- normalized sync score from the framing stage
- normalized sync margin when a second-best sync candidate exists
- clipped-symbol fraction from the demodulator
- CRC pass or fail

`compute_trust_score` combines those features into a scalar value in `[0, 1]`
with fixed weights:

- `0.20` LLR strength
- `0.20` LLR consistency
- `0.15` sync quality
- `0.15` sync-margin quality
- `0.05` demod quality
- `0.25` CRC quality

CRC failure caps the score at `0.35`, so a replay with failed integrity checks
cannot appear highly trusted.

The replay output also reports a simple trust-assessment band:

- `high-confidence`
- `guarded`
- `low-confidence`

The checked-in impaired replay fixture is useful here. It still decodes to the
same payload as the healthy case, but it produces lower mean soft-bit strength,
a higher weak-LLR fraction, and a `guarded` trust assessment. That makes the
trust output useful even before the decode path fails outright.

This is still a compact demo heuristic. It is meant to be inspectable and
reproducible for the canned replay inputs, not an operational anomaly detector
or mission-grade link-health model.
