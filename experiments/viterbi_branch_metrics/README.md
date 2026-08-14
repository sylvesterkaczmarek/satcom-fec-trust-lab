# Historical Viterbi branch-metric experiment

This directory preserves a narrow comparison of branch-metric preparation for
the downstream convolutional decoder. It is not the project's primary
optimization workload and is not evidence of end-to-end FEC acceleration.

The experiment compares identical prepared soft decisions through:

- scalar reference branch metrics;
- NEON branch metrics when compiled for AArch64;
- a locally streaming SVE-style branch-metric kernel when compiled with the
  SME2 feature gate.

The streaming-vector kernel does not use ZA or an SME2-specific multi-vector
operation. The Viterbi add-compare-select recurrence and traceback are scalar
for every path. Reported timings are local microbenchmark observations only.

Run the correctness-aligned timing experiment with:

```sh
bash experiments/viterbi_branch_metrics/run.sh
```

The supported optimization and benchmark workload is IQ-domain acquisition;
see `docs/acquisition_design.md` and `docs/benchmarking.md`.
