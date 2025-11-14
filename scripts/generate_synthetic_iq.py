#!/usr/bin/env python3
"""
Generate a small synthetic BPSK IQ clip that matches the simple stub pipeline.

- Bits are random.
- BPSK mapping: 0 -> -1, 1 -> +1 on I, Q = 0.
- Output format: interleaved float32 I, Q.

This is intended as a lightweight demo, not a mission accurate waveform.
"""

import argparse
import struct
import random

def generate_bpsk_iq(num_symbols: int) -> bytes:
    buf = []
    for _ in range(num_symbols):
        bit = random.getrandbits(1)
        val = 1.0 if bit == 1 else -1.0
        # I, Q
        buf.append(struct.pack("<ff", val, 0.0))
    return b"".join(buf)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=str,
        default="synthetic_bpsk.iq",
        help="Output file path for IQ samples",
    )
    parser.add_argument(
        "--symbols",
        type=int,
        default=4096,
        help="Number of BPSK symbols to generate",
    )
    args = parser.parse_args()

    iq_bytes = generate_bpsk_iq(args.symbols)
    with open(args.output, "wb") as f:
        f.write(iq_bytes)

    print(f"Written {args.symbols} symbols to {args.output}")

if __name__ == "__main__":
    main()
