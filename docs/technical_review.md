# Technical review guide

## Public scope

The supported project is a host-side synthetic-IQ receiver replay and
acquisition experiment. It includes preamble acquisition over timing and CFO
hypotheses, aligned BPSK demodulation, a small convolutional-code/CRC replay,
and explainable trust diagnostics. A separate Android NDK command-line target
allows the acquisition benchmark to run through ADB; it is not an Android app.

The acquisition implementations are:

- `reference`: scalar float64 correlation oracle in
  `src/acquisition/acquisition_reference.cpp`;
- `neon`: float32 NEON complex correlation over timing tiles in
  `src/acquisition/acquisition_neon.cpp`;
- `sme2`: float32 ZA VGx4 accumulation in
  `src/acquisition/acquisition_sme2_kernel.cpp`, with runtime checks and generic
  workspace preparation in `src/acquisition/acquisition_sme2.cpp`. It is
  available only when both compilation and runtime feature checks succeed.

Requested accelerated acquisition paths report `unavailable` rather than
executing a scalar fallback under an accelerated name.

## Complete correctness check

From a clean checkout with CMake 3.22+, a C++17 compiler, Python 3, and `jq`:

```bash
make verify
```

This verifies fixture checksums, builds with `-Wall -Wextra -Wpedantic
-Werror`, checks per-source compile commands, runs CTests and Python tests,
executes replay/trust/FEC validation scripts, and repeats the portable CTests
under ASan and UBSan. Architecture-specific execution is attempted only when
the local compiler and host expose the required features.

Fixture seeds are stored in each JSON sidecar. IQ and preamble SHA-256 values
are stored both in those sidecars and in
`data/synthetic/fixture_manifest.json`. Verify them independently with:

```bash
make verify-fixtures
```

## Build separation evidence

CMake exports `compile_commands.json`. Architecture options are source
properties, not global compiler flags:

- reference correlation receives explicit loop and SLP auto-vectorization
  controls;
- NEON target flags and compiled-kernel definitions are limited to NEON source
  files;
- SME2 target flags are limited to
  `src/acquisition/acquisition_sme2_kernel.cpp` and the historical locally streaming
  `src/fec/branch_metrics_streaming_vector.cpp` experiment. Only the
  acquisition kernel source contains the checked ZA VGx4 SME2 mechanism. The
  runtime guard and optional workspace preparation compile without an SVE/SME
  target.

Check any configured build with:

```bash
python3 scripts/check_compile_commands.py \
  --build-dir build/host_replay \
  --expect portable
```

Use `--expect neon` or `--expect sme2` for the corresponding explicit build.
The command fails if an SME2 flag reaches the scalar reference, NEON
comparison, or generic FEC source.

## Numerical equivalence

```bash
bash scripts/check_acquisition_neon.sh
bash scripts/check_sme2_acquisition.sh
python3 -m unittest discover -s tests -v
```

The direct kernel checks cover candidate identity, complex correlation
components, scores, odd and non-vector-width lengths, tails, weak values, and
high finite amplitudes. Fixture tests compare detected timing, CFO, best and
second-best candidates, and bounded score differences. SME2 execution is not
reported when runtime support is absent.

## Instruction evidence

On a native SME2 host:

```bash
bash scripts/verify_sme2_acquisition_assembly.sh
```

The verifier requires streaming-mode boundaries, ZA transfers, VGx4
`FMLA`/`FMLS`, and vector IQ load/deinterleave evidence. It also checks that the
NEON comparison and generic SME2 control/workspace objects contain no checked
SVE/SME patterns, and that the scalar reference does not contain the SME2
sequence.

With an Android NDK, the Android cross-build supplies compile and object
evidence without executing the binary:

```bash
bash scripts/verify_android_benchmark_build.sh --sme2 auto
```

Use `--sme2 on` when SME2 compilation is a required property of the installed
NDK. Macro presence alone is not accepted as instruction evidence.

## Benchmarking

```bash
make benchmark-acquisition
```

The benchmark validates each implementation against the scalar oracle before
timing. JSON records the source commit and dirty state, CMake generator,
compiler path/ID/version/target, build options, common and source-specific
flags, host metadata, runtime feature detection, workload, correctness,
workspace bytes, execution order, and raw timing samples. See
`docs/benchmarking.md` for the fixed workloads and fairness contract.

CI benchmark invocations are smoke tests only. Tracked reports under
`benchmarks/results/` are local measurements whose hardware and build metadata
must be read from each report. No CI timing is publication evidence.

`benchmarks/results/a83cd53/` is retained unchanged as historical evidence for
an earlier packed SME2 input path and earlier NEON baseline. Its results remain
valid for the recorded source commit, host, and compiler, but they are not
current-kernel performance evidence. Read each result directory's metadata
before using any timing value.

## Not claimed

- no live RF or mission-derived capture support;
- no calibrated detection probability or operational trust threshold;
- no Android application or end-to-end mobile replay;
- no SVE acquisition implementation;
- no SME2 Viterbi recurrence or traceback; the historical FEC path is locally
  streaming SVE-style branch-metric preparation, not genuine SME2-specific
  acceleration;
- no general NEON or SME2 speedup, thermal, energy, or cross-device result; the
  tracked Apple measurements are explicitly local;
- no claim that direct correlation is optimal for long windows, long preambles,
  or dense CFO grids; FFT/filter-bank and hierarchical searches are not
  implemented here.
