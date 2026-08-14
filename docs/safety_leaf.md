# Safety Notes

This repository is a developer demo for offline replay and decoder plumbing. It
is not a certified communications or mission-support system.

Use the trust score as a compact diagnostic for the canned replay path, not as a
standalone authority. The score is derived from explicit acquisition,
soft-decision, secondary frame-sync, demodulation, and CRC evidence.
Acquisition confidence is an uncalibrated diagnostic, not a probability of
correct reception.
