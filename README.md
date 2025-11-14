# satcom-fec-trust-lab

Pocket sized satellite lab on Arm mobile. A phone with an RTL-SDR decodes open satellite signals with LDPC and Viterbi on SME2, compares against a NEON baseline, and computes a simple link trust score. Includes synthetic IQ clips and configs for Φsat-2 compatible replay and Arm cloud runs.

All code, configuration presets, and synthetic example datasets live in this repository:  
https://github.com/sylvesterkaczmarek/satcom-fec-trust-lab

The aim is to give developers a practical, space relevant example of Arm SME2 in action that fits in a pocket and supports trustworthy autonomy and RPO / ISAM style comms testing.

## Features

- Satcom receive and decode path that runs on an Armv9 phone with SME2 support  
- LDPC and Viterbi FEC with SME2 vector kernels and a NEON baseline for comparison  
- Trust monitors that compute simple link health features and a scalar trust score  
- Replay path for Φsat-2 like links using synthetic IQ recordings  
- Optional Arm based cloud path for batch analysis on Graviton  
- Scripts and notebooks for metrics, plots, and trace inspection

## High level architecture

- Phone + RTL-SDR capture open VHF / UHF satellite signals intended for public reception  
- C / C++ core handles DSP, framing, demod, and FEC through JNI  
- SME2 code paths accelerate the inner loops of LDPC and Viterbi  
- Trust layer computes features such as spectral kurtosis, preamble SNR, LLR stats and FER trend  
- Kotlin UI shows lock status, basic metrics, and trust score over time  
- Same decoder and trust logic can run on Arm cloud instances for long recordings

## Status

This repo is a work in progress. The first milestone is a minimal end to end pipeline:

1. Load a canned IQ file of an open satellite demo signal  
2. Run the full DSP and FEC chain on device with NEON  
3. Switch to SME2 kernels and compare throughput and energy per bit  
4. Produce a simple plot and a trust score timeline

Subsequent milestones add live RTL-SDR capture, Φsat-2 compatible synthetic waveforms, and richer trust features.

## Repo layout

Planned structure:

```text
satcom-fec-trust-lab/
  .gitignore
  LICENSE
  README.md

  app/
    src/
      main/
        java/com/sylvesterkaczmarek/satcomfec/...
        cpp/...
        assets/...

  data/
    synthetic/
    examples/

  scripts/
  docs/
  .github/
  ci/
  tests/
