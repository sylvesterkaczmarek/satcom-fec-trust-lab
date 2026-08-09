# SME2 acquisition kernel

## Correlation blocking

The acquisition search computes one complex correlation for every timing/CFO
pair. For a fixed CFO and preamble sample, its complex matched-filter weight is
shared by all timing hypotheses. The SME2 path therefore uses timing as its
parallel dimension rather than expanding a separate weight vector for every
candidate.

`prepare_sme2_acquisition_workspace` packs received samples as two float32
sample-major arrays:

```text
received_real[sample][timing]
received_imag[sample][timing]
```

The acquisition plan stores precomputed matched-filter weights as
`weight_real[cfo][sample]` and `weight_imag[cfo][sample]`. Packing, allocation,
and CFO-weight generation occur before the prepared kernel call.

## SME2 mechanism

`src/acquisition/acquisition_sme2.cpp` processes `4 * SVL.W` timing hypotheses
per batch. Four scalable vectors are transferred to a ZA vector group. Two ZA
groups hold the real and imaginary correlation accumulators:

```text
real += received_real * weight_real
real -= received_imag * weight_imag
imag += received_real * weight_imag
imag += received_imag * weight_real
```

The implementation uses SME2 VGx4 `FMLA` and `FMLS` ACLE intrinsics targeting
ZA, plus VGx4 ZA write/read intrinsics. Predicated loads and stores handle any
timing count, including a final batch shorter than the streaming vector length.
This is a ZA-backed SME2 multi-vector kernel, not an SVE loop labeled as SME2.

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
32 * float_epsilon * preamble_length *
    sum(abs(received[n]) * abs(weight[n]))
```

The magnitude-squared score tolerance is propagated from that component bound.
Tests also require identical best and second-best timing/CFO identities across
reference, NEON when compiled, and SME2.

## Scalar work and availability

Workspace preparation, score calculation, confidence calculation, and top-two
candidate selection remain scalar and are outside the SME2 correlation kernel.
The fixture CLI is an end-to-end functional demo and therefore includes setup.
The separate benchmark can measure setup-inclusive and prepared steady-state
execution, but the repository does not publish a general speedup conclusion.

The SME2 translation unit is enabled only with `SATCOMFEC_ENABLE_SME2=ON`, a
compiler defining `__ARM_FEATURE_SME2`, and ACLE support for ZA VGx4
multiply-accumulate intrinsics. Runtime execution additionally requires SME2
hardware reported by the operating system. An unsupported request returns
`implementation = "unavailable"`; it never runs a scalar fallback under the
SME2 label.

Verification commands:

```bash
bash scripts/check_sme2_acquisition.sh --require-sme2
bash scripts/verify_sme2_acquisition_assembly.sh
```

The second command requires emitted `smstart`/`smstop`, VGx4 ZA `fmla`/`fmls`,
and ZA transfer instructions. A feature macro alone does not satisfy the
assembly check.

## Arm references

- [Arm C Language Extensions](https://arm-software.github.io/acle/main/acle.html)
- [Arm Scalable Matrix Extension Programmer's Guide](https://documentation-service.arm.com/static/664f013638084307512bb30c)
