# Pocket Satcom acquisition and trust summary

The repository is centered on one synthetic replay story:

- load a checked-in IQ file
- acquire a known IQ preamble across timing and CFO hypotheses
- compensate the selected CFO and carrier phase
- confirm aligned framing with the legacy soft sync word
- decode a convolutionally coded frame
- confirm the payload with CRC
- report a trust score alongside the decoded text

That scope is intentionally narrow. The public demo favors a runnable,
inspectable path over broader claims about hardware acceleration, live RF
capture, or mission-specific replay.
