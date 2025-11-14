# Story narrative

This document tracks the high level story used for the Arm Ambassador use case.

The authoritative version of the narrative is maintained in the Word document shared with Arm. This file is a short repository side summary.

## Summary

Pocket sized satellite lab on Arm mobile. A phone and an RTL-SDR decode open VHF / UHF satellite signals on device, run LDPC and Viterbi with SME2 acceleration and a NEON baseline, and compute a simple trust score that can abstain under uncertainty. Synthetic Φsat-2 like waveforms provide a mission inspired replay path without redistributing proprietary RF.

The full blog style narrative covers:

- Motivation from RPO / ISAM rehearsals and link health  
- Architecture of the DSP, FEC, and trust pipeline  
- SME2 vs NEON comparisons for throughput and energy per bit  
- Data and licensing constraints for live and replay modes  
- Next steps and learning path ideas
