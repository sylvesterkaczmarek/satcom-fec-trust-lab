# SME2 acquisition kernel

## Correlation blocking

The acquisition search computes one complex correlation for every timing/CFO
pair. For a fixed CFO and preamble sample, its complex matched-filter weight is
shared by all timing hypotheses. The SME2 path therefore uses timing as its
parallel dimension rather than expanding a separate weight vector for every
candidate.

For the consecutive timing grids used by replay and the fixed benchmark,
samples for adjacent timing hypotheses are already adjacent in interleaved IQ.
The kernel reads them directly with predicated `LD2W` loads; no input copy or
sample-major workspace is required.

Arbitrary non-contiguous timing grids cannot use that layout. For those grids,
`prepare_sme2_acquisition_workspace` explicitly packs complex float32 samples
as:

```text
received_by_sample_and_timing[sample][timing]
```

The acquisition plan stores precomputed matched-filter weights as
`weight_real[cfo][sample]` and `weight_imag[cfo][sample]`. CFO-weight generation
occurs before the prepared kernel call. The workspace reports whether packing
was required, and per-capture timing includes it when it is required.

## SME2 mechanism

`src/acquisition/acquisition_sme2_kernel.cpp` processes `4 * SVL.W` timing hypotheses
per batch. Four scalable vectors are transferred to a ZA vector group. Two ZA
groups hold the real and imaginary correlation accumulators:

```text
real += received_real * weight_real
real -= received_imag * weight_imag
imag += received_real * weight_imag
imag += received_imag * weight_real
```

The implementation uses SME2 VGx4 `FMLA` and `FMLS` ACLE intrinsics targeting
ZA, plus VGx4 ZA write/read intrinsics. Predicated `LD2W` loads deinterleave
direct IQ; the packed path uses predicated complex loads. Predicated stores
handle any timing count, including a final batch shorter than the streaming
vector length. This is a ZA-backed SME2 multi-vector kernel, not an SVE loop
labeled as SME2.

The kernel does not use `FMOPA`. A full timing-by-CFO outer-product tile would
consume the float32 ZA tile for one complex component at a time, requiring
separate accumulation and extraction passes for real and imaginary results.
The VGx4 formulation instead matches the fixed-CFO inner product directly and
keeps both complex components live in separate ZA vector groups. This is a
correctness-oriented blocking decision, not a performance conclusion.

The kernel function is `__arm_locally_streaming` and declares new ZA state. A
single streaming region covers every CFO and timing batch in one prepared
acquisition run; streaming mode is not entered inside the sample or hypothesis
loops.

## Numerical equivalence

The scalar reference uses double-precision weights and accumulation. NEON and
SME2 use the same precomputed float32 weights and float32 accumulation. Direct
tests compare every complex correlation component with this forward-error
bound:

```text
8 * float_epsilon * preamble_length *
    sum(abs(received[n]) * abs(weight[n]))
```

The magnitude-squared score tolerance is propagated from that component bound.
Tests also require identical best and second-best timing/CFO identities across
reference, NEON when compiled, and SME2.

## Scalar work and availability

Workspace preparation, score calculation, confidence calculation, and top-two
candidate selection remain scalar and are outside the SME2 correlation kernel.
The fixture CLI is an end-to-end functional demo and therefore includes setup.
The benchmark measures prepared steady-state execution, per-capture execution,
and setup-inclusive execution. For consecutive timing grids, per-capture SME2
execution does not pack input; for non-contiguous grids, the required packing
is timed and reported.

For the fixed benchmark grids, dynamic SME2 storage is the complex correlation
output: `2 * CFO hypotheses * timing hypotheses * sizeof(float)`. This is
40,960 bytes for `small`, 294,912 for `medium`, 2,228,224 for `large`, and
6,553,600 for `very-large`. An arbitrary timing grid additionally requires
`timing hypotheses * preamble length * sizeof(ComplexF)` bytes of packed input.
Allocator bookkeeping, stack state, and ZA register storage are not counted.

`benchmarks/results/b6ed1ec/` contains the current five-process Apple M5 Pro
timing, correctness, and memory evidence for this direct-input kernel and the
audited NEON baseline. `benchmarks/results/a83cd53/` is retained unchanged as
historical evidence for its prior packed-input SME2 implementation and prior
NEON baseline. Timing and memory values must be attributed to the source commit
recorded by each result.

The target-specific SME2 translation unit is enabled only with
`SATCOMFEC_ENABLE_SME2=ON`, a compiler defining `__ARM_FEATURE_SME2`, and ACLE
support for ZA VGx4 multiply-accumulate intrinsics. Runtime feature detection,
validation, score selection, and optional workspace packing remain in the generic
`src/acquisition/acquisition_sme2.cpp` translation unit without an SVE/SME
target. Runtime execution additionally requires SME2 hardware reported by the
operating system. An unsupported request returns `implementation =
"unavailable"`; it never runs a scalar fallback under the SME2 label.

Verification commands:

```bash
bash scripts/check_sme2_acquisition.sh --require-sme2
bash scripts/verify_sme2_acquisition_assembly.sh
```

The second command requires emitted `smstart`/`smstop`, VGx4 ZA `fmla`/`fmls`,
ZA transfer instructions, and vector IQ load/deinterleave evidence. It also
rejects checked SVE/SME instructions in the generic control/workspace object.
A feature macro alone does not satisfy the assembly check.

## Arm references

- [Arm C Language Extensions](https://arm-software.github.io/acle/main/acle.html)
- [Arm Scalable Matrix Extension Programmer's Guide](https://documentation-service.arm.com/static/664f013638084307512bb30c)
