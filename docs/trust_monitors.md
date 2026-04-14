# Trust Monitors

The replay demo computes three trust features for the decoded frame:

- mean absolute LLR over the coded frame
- normalized sync correlation score
- CRC pass or fail

`compute_trust_score` combines those features into a scalar value in `[0, 1]`.
CRC failure caps the score, so a frame with weak integrity evidence cannot
appear highly trusted.

This score is a simple demo heuristic. It is useful for the canned replay path,
but it is not presented as an operational link-health model.
