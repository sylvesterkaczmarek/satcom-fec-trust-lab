# IQ acquisition design

## Problem definition

The acquisition subsystem searches for a known complex preamble in a received
IQ window. It evaluates a configured grid of integer sample offsets and
carrier-frequency-offset (CFO) hypotheses, then reports the strongest and
second-strongest candidates. This is an offline, grid-based correctness
reference; it is not a tracking loop or a complete receiver synchronizer.

## Reference correlation

For received samples `r`, preamble samples `p`, timing hypothesis `tau`, CFO
hypothesis `f`, sample rate `Fs`, and preamble length `L`, the reference kernel
computes

```text
C(tau, f) = sum(n=0..L-1) r[tau + n] * conj(p[n]) * exp(-j 2 pi f n / Fs)
S(tau, f) = |C(tau, f)|^2
```

`S` is the candidate score. `prepare_acquisition_plan` validates the
configuration and precomputes `conj(p[n]) * exp(-j 2 pi f n / Fs)` once for
each CFO hypothesis. `run_reference_acquisition` performs the scalar dot
products and retains the two largest scores. Peak ratio is `Sbest / Ssecond`;
normalized peak separation is `(Sbest - Ssecond) / Sbest`.

The CFO sign convention matches the fixture generator: a preamble injected
with `exp(+j 2 pi f n / Fs)` is corrected by the negative exponential above.

## Fixture design

`scripts/generate_acquisition_fixtures.py` produces a deterministic 256-sample
QPSK preamble and 4,096-sample complex-IQ captures. Every JSON sidecar records
the random seed, noise level, amplitude, phase, timing offset, CFO, search grid,
fading depth, optional distractor, and IQ format.

The checked-in classes are:

- `clean`: high-SNR, zero-CFO acquisition
- `noisy`: lower-SNR acquisition at `-500 Hz`
- `frequency_offset`: acquisition at `1500 Hz`
- `ambiguous`: a primary signal plus a weaker delayed/CFO-shifted copy
- `weak_faded`: reduced amplitude with deterministic intra-preamble fading

These are synthetic engineering fixtures. They are not recordings from a
radio, spacecraft, or mission waveform.

## Correctness criteria

The injected timing and CFO values lie exactly on the configured search grid.
A fixture passes when the maximum-score candidate equals both ground-truth
values. Tests also require a valid lower-scoring runner-up, require the
ambiguous fixture's runner-up to match its injected distractor, and regenerate
all fixture bytes twice to verify deterministic output.

The scalar kernel is the correctness oracle. A separate Arm NEON translation
unit evaluates the same candidate grid using four-lane float32 complex
multiply-accumulate operations and a scalar tail for lengths not divisible by
four. The NEON path uses float32 prepared weights and accumulation, while the
reference path retains double-precision weights and accumulation. Supporting
GCC/Clang builds disable loop and SLP auto-vectorization specifically for the
reference translation unit; Arm target flags are applied only to intrinsic
sources.

The opt-in SME2 translation unit evaluates the same grid with float32 weights
and accumulation. It groups timing hypotheses into four scalable vectors and
uses SME2 VGx4 multiply-add and multiply-subtract operations with ZA
accumulators. Its packed workspace is prepared before the kernel call. Score
calculation and top-two candidate selection remain scalar. The implementation
and streaming boundary are specified in `docs/sme2_acquisition.md`.

The CLI accepts `--implementation reference|neon|sme2`. A NEON or SME2 request
on a build without the selected intrinsic kernel returns
`implementation = "unavailable"`; it does not execute or label a scalar
fallback.

Fixture equivalence requires identical best and second-best timing/CFO
candidates. Best and second-best scores use an absolute tolerance of `1e-3`
plus a relative tolerance of `2e-4`. Direct deterministic kernel cases use a
forward-error bound per real/imaginary correlation component:

```text
32 * float_epsilon * preamble_length *
    sum(abs(received[n]) * abs(weight[n]))
```

The score bound is propagated from that complex-correlation bound. The safety
factor covers float32 weight rounding, accumulation order, fused multiply-add
behavior, and path-specific tail handling.

## Workload size

The default search evaluates 3,841 timing hypotheses and 9 CFO hypotheses over
a 256-sample complex preamble: 34,569 candidate correlations and 8,849,664
complex multiply-accumulate terms per capture. Unlike the existing short
Viterbi branch-metric experiment, this workload operates on a longer IQ window
and exposes independent work across timing/CFO candidates and preamble samples.
This observation defines the workload shape only; it is not a performance or
acceleration claim.
