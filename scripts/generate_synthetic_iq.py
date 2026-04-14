#!/usr/bin/env python3
"""
Generate the deterministic canned replay asset used by the public demo.

The frame format is intentionally small and fully documented in the repo:
- 16-bit sync word
- ASCII payload
- CRC-8 over the payload bytes
- Rate-1/2 convolutional encoding with generators (7, 5) in octal
- BPSK modulation with fixed oversampling
"""

from __future__ import annotations

import argparse
import json
import math
import random
import struct
from pathlib import Path


SYNC_WORD = [1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1]
MESSAGE = b"SATCOM DEMO OK"
SAMPLES_PER_SYMBOL = 8
SEED = 7


def crc8_bytes(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            msb = crc & 0x80
            crc = (crc << 1) & 0xFF
            if msb:
                crc ^= 0x07
    return crc


def bytes_to_bits(data: bytes) -> list[int]:
    bits: list[int] = []
    for value in data:
        for bit in range(7, -1, -1):
            bits.append((value >> bit) & 1)
    return bits


def parity3(value: int) -> int:
    value ^= value >> 1
    value ^= value >> 2
    return value & 1


def convolutional_encode(bits: list[int]) -> list[int]:
    state = 0
    encoded: list[int] = []
    for bit in bits + [0, 0]:
        shift_reg = ((state << 1) | bit) & 0b111
        encoded.append(parity3(shift_reg & 0b111))
        encoded.append(parity3(shift_reg & 0b101))
        state = ((state << 1) | bit) & 0b11
    return encoded


def build_frame_bits() -> list[int]:
    payload = MESSAGE + bytes([crc8_bytes(MESSAGE)])
    return SYNC_WORD + convolutional_encode(bytes_to_bits(payload))


def modulate_bpsk(frame_bits: list[int], rng: random.Random) -> bytes:
    samples: list[bytes] = []
    for symbol_index, bit in enumerate(frame_bits):
        symbol = 1.0 if bit else -1.0
        symbol_gain = 1.0 + 0.05 * math.sin(symbol_index / 7.0)
        for _ in range(SAMPLES_PER_SYMBOL):
            i_val = symbol * symbol_gain + 0.03 + rng.gauss(0.0, 0.04)
            q_val = -0.02 + rng.gauss(0.0, 0.02)
            samples.append(struct.pack("<ff", i_val, q_val))
    return b"".join(samples)


def write_metadata(path: Path, frame_bits: list[int]) -> None:
    payload = MESSAGE + bytes([crc8_bytes(MESSAGE)])
    metadata = {
        "message": MESSAGE.decode("ascii"),
        "message_bytes": len(MESSAGE),
        "payload_bytes_with_crc": len(payload),
        "sync_word_bits": SYNC_WORD,
        "samples_per_symbol": SAMPLES_PER_SYMBOL,
        "coded_bits_per_frame": len(frame_bits) - len(SYNC_WORD),
        "seed": SEED,
    }
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/synthetic/canned_replay/demo_conv_bpsk.iq"),
        help="Path to the generated float32 IQ file",
    )
    parser.add_argument(
        "--metadata",
        type=Path,
        default=Path("data/synthetic/canned_replay/demo_conv_bpsk.json"),
        help="Path to the metadata JSON companion file",
    )
    args = parser.parse_args()

    frame_bits = build_frame_bits()
    rng = random.Random(SEED)
    iq_bytes = modulate_bpsk(frame_bits, rng)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(iq_bytes)
    args.metadata.parent.mkdir(parents=True, exist_ok=True)
    write_metadata(args.metadata, frame_bits)

    print(f"Wrote {args.output}")
    print(f"Wrote {args.metadata}")


if __name__ == "__main__":
    main()
