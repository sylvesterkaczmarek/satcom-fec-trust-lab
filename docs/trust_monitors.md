# Trust monitors

Replay JSON reports inspectable evidence from IQ acquisition, demodulation,
secondary frame sync, and CRC. These are deterministic engineering heuristics
for the checked-in fixtures, not probabilities or operational anomaly models.

Acquisition features:

- normalized peak: correlation magnitude squared divided by preamble energy
  and candidate-window energy;
- peak separation: `(best_score - second_score) / best_score`;
- timing ambiguity: `1 - peak_separation`;
- residual uncertainty: `1 - confidence`, where confidence is
  `sqrt(normalized_peak) * peak_separation`;
- acquisition accepted: normalized peak and separation exceed explicit replay
  thresholds.

The output sets `confidence_calibrated=false`. Confidence orders the
deterministic examples; it is not a probability of correct acquisition.

Downstream features:

- mean absolute bounded soft decision and its fixed normalization;
- fraction of framed soft decisions with absolute value below `48`;
- normalized 16-bit secondary frame-sync score and margin;
- clipped-symbol fraction;
- CRC evaluated and CRC pass state.

The BPSK demodulator outputs signed integer soft decisions. They are not
calibrated log-likelihood ratios and are not labeled as LLRs in the public JSON.
The secondary frame-sync check occurs after IQ acquisition and demodulation; it
is not the acquisition search.

Score weights:

- `0.15` soft-decision strength;
- `0.15` soft-decision consistency;
- `0.15` acquisition strength;
- `0.15` acquisition separation;
- `0.10` acquisition certainty;
- `0.05` secondary frame-sync quality;
- `0.05` secondary frame-sync margin;
- `0.05` demodulation quality;
- `0.15` CRC quality.

Acquisition rejection caps the score at `0.15`. An evaluated CRC failure caps
it at `0.35`. The fixture expectations are:

- `healthy`: exact timing/CFO, clear peak, CRC pass, high confidence;
- `impaired`: exact timing/CFO and CRC pass with weaker acquisition and soft
  decisions;
- `ambiguous`: exact timing/CFO and CRC pass with a competing peak;
- `failed`: accepted acquisition followed by coded-data corruption and CRC
  failure;
- `no_signal`: weak acquisition rejected before demodulation.

The `high-confidence`, `guarded`, and `low-confidence` bands summarize those
checks. They are not mission-assurance levels.
