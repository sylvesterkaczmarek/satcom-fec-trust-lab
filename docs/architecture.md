# Architecture overview

The project has three main layers.

1. Capture and front end  
   - Phone connects to an RTL-SDR over USB.  
   - IQ samples stream into a C / C++ front end that currently passes them through but later hosts filtering, resampling, and carrier recovery.

2. Demodulation and FEC  
   - A simple BPSK soft demodulator converts complex samples into signed 8 bit soft bits.  
   - Framing logic identifies frame boundaries in the soft stream.  
   - LDPC and Viterbi decoders consume frame soft bits and emit hard bits.  
   - Each decoder has two build variants: SME2 vector kernels and a NEON baseline.

3. Trust layer and UI  
   - Trust features (for example mean LLR and short window FER) are computed over decoded windows.  
   - A scalar trust score is produced from these features and used to highlight low confidence regions.  
   - The Android UI shows basic status, metrics, and trust score trends.

A more detailed diagram with call flow and JNI boundaries will live here once the code stabilises.
