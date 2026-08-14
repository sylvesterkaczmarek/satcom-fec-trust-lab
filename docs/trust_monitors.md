# Trust monitors

The replay demo reports inspectable diagnostics from acquisition, demodulation,
secondary framing, and CRC. They are deterministic engineering heuristics for
the checked-in fixtures, not calibrated probabilities or operational anomaly
detection.

Acquisition features:

- normalized acquisition peak: correlation magnitude squared divided by
  preamble energy times candidate-window energy
- acquisition peak separation: `(best_score - second_score) / best_score`
- timing ambiguity: `1 - peak_separation`
- residual acquisition uncertainty: `1 - confidence`, where confidence is
  `sqrt(normalized_peak) * peak_separation`
- acquisition accepted: both normalized peak and peak separation passed their
  explicit replay thresholds

The confidence value is deliberately reported with
`confidence_calibrated=false`. It is useful for ordering these deterministic
cases, but it is not a probability of correct acquisition.

Downstream features:

- mean absolute LLR and normalized mean absolute LLR
- fraction of framed soft bits with `|LLR| < 48`
- normalized 16-bit secondary sync score and margin
- clipped-symbol fraction
- whether CRC was evaluated and whether it passed

The score combines bounded component values with fixed weights:

- `0.15` LLR strength
- `0.15` LLR consistency
- `0.15` acquisition strength
- `0.15` acquisition separation
- `0.10` acquisition certainty
- `0.05` secondary sync quality
- `0.05` secondary sync-margin quality
- `0.05` demod quality
- `0.15` CRC quality

Acquisition rejection caps the score at `0.15`. An evaluated CRC failure caps
it at `0.35`. A noise-only capture therefore reports acquisition rejection and
`crc_not_evaluated`, while the corrupted-frame fixture reports a real
`crc_failed` condition.

Checked fixture behavior:

- `healthy`: exact timing/CFO, clear peak, CRC pass, high confidence
- `impaired`: exact timing/CFO and CRC pass with weaker acquisition and soft-bit
  evidence
- `ambiguous`: exact timing/CFO and CRC pass, but a close distractor peak
- `failed`: exact acquisition followed by coded-data corruption and CRC failure
- `no_signal`: low normalized peak, rejected before demodulation

The `high-confidence`, `guarded`, and `low-confidence` bands summarize those
explicit checks. They are not mission assurance levels.
