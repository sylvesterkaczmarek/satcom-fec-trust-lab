# Tracked benchmark results

This directory preserves local acquisition benchmark evidence that would
otherwise remain under ignored `build/` output. Each result set contains the
unaltered JSON from every independent process and a derived repeatability
summary. The raw reports are authoritative.

- [`a83cd53/`](a83cd53/README.md): five historical SME2-capable runs of source commit
  `a83cd53ffe153fa69329194174f735d0a972380d` on the host identified in the
  reports as Apple M5 Pro (`Mac17,9`). This result measured the earlier packed
  SME2 input path and earlier NEON baseline.

Historical files are not rewritten after kernel changes. Their measurements do
not establish current-kernel performance or a result for another processor,
operating system, compiler, capture distribution, or waveform.
