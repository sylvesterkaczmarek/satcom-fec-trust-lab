# Trust monitors

Replay JSON reports inspectable evidence from IQ acquisition, demodulation,
secondary frame sync, and CRC. These are deterministic engineering heuristics
for the checked-in fixtures, not probabilities or operational anomaly models.

Acquisition features:

- normalized peak: correlation magnitude squared divided by preamble energy
  and candidate-window energy;
- distinct peak separation: `(best_score - distinct_competitor_score) / best_score`.
  A distinct competitor has a different timing or a CFO at least
  `sample_rate_hz / preamble_length` from the winning CFO.
  A missing competitor gives no separation evidence and the default gate rejects acquisition.
  The separation and certainty components are `null` and contribute no weight.
  A native caller that disables the separation gate can receive at most a guarded
  assessment without a distinct competitor;
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
- normalized 16-bit secondary frame-sync score, with a margin when two offsets
  have been tested;
- fraction of BPSK symbol means outside the fixed demodulator full scale,
  when a scale has been supplied;
- CRC evaluated and CRC pass state.

The BPSK demodulator outputs signed integer soft decisions. They are not
calibrated log-likelihood ratios and are not labeled as LLRs in the public JSON.
The secondary frame-sync check occurs after IQ acquisition and demodulation; it
is not the acquisition search.

The aligned replay contains one possible frame start. It still measures the
16-bit sync score. With no second candidate, the margin, runner-up and
frame-sync ambiguity flag are `null`. The default demodulator scales the
largest symbol in the current frame to 127. It has no independent clipping
threshold, so its clipped-symbol count, fraction and flag are also `null`.
To use a fixed full scale, pass `--symbol-full-scale N` to `replay_demo` or
`scripts/run_replay_demo.sh`. Supply a finite positive magnitude in units of
the real BPSK symbol mean after front-end RMS normalisation and carrier/phase
correction. The chosen value appears as `demod.full_scale_symbol_mean`.
Clipping counts symbol means whose absolute value exceeds that scale before
quantisation. This measures soft-decision saturation. ADC saturation requires
separate evidence at the capture stage.

Weights of measured components (unavailable components are omitted and the
weighted sum is divided by the sum of the available weights):

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
