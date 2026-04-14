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
from dataclasses import dataclass
import json
import math
import random
import struct
from pathlib import Path


SYNC_WORD = [1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1]
MESSAGE = b"SATCOM DEMO OK"
SAMPLES_PER_SYMBOL = 8


@dataclass(frozen=True)
class ScenarioConfig:
    name: str
    seed: int
    i_dc: float
    q_dc: float
    i_noise_stddev: float
    q_noise_stddev: float
    gain_ripple: float
    fade_start: int | None = None
    fade_stop: int | None = None
    fade_scale: float = 1.0
    phase_ripple_degrees: float = 0.0
    corruption_start_symbol: int | None = None
    corruption_stop_symbol: int | None = None
    corruption_mode: str = "none"
    description: str = ""


SCENARIOS = {
    "healthy": ScenarioConfig(
        name="healthy",
        seed=7,
        i_dc=0.03,
        q_dc=-0.02,
        i_noise_stddev=0.04,
        q_noise_stddev=0.02,
        gain_ripple=0.05,
        description="Baseline replay used by the public host-side quick start.",
    ),
    "impaired": ScenarioConfig(
        name="impaired",
        seed=29,
        i_dc=0.05,
        q_dc=-0.03,
        i_noise_stddev=0.20,
        q_noise_stddev=0.08,
        gain_ripple=0.25,
        fade_start=88,
        fade_stop=124,
        fade_scale=0.28,
        phase_ripple_degrees=16.0,
        description=(
            "Deterministic impaired replay with added noise, stronger amplitude ripple, "
            "and a short mid-frame fade."
        ),
    ),
    "failed": ScenarioConfig(
        name="failed",
        seed=41,
        i_dc=0.04,
        q_dc=-0.02,
        i_noise_stddev=0.10,
        q_noise_stddev=0.05,
        gain_ripple=0.12,
        fade_start=92,
        fade_stop=132,
        fade_scale=0.35,
        phase_ripple_degrees=10.0,
        corruption_start_symbol=56,
        corruption_stop_symbol=120,
        corruption_mode="invert",
        description=(
            "Deterministic failed replay with an intact sync region but a corrupted "
            "coded-data segment that drives CRC failure."
        ),
    ),
}


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


def modulate_bpsk(frame_bits: list[int], scenario: ScenarioConfig) -> bytes:
    rng = random.Random(scenario.seed)
    samples: list[bytes] = []
    for symbol_index, bit in enumerate(frame_bits):
        symbol = 1.0 if bit else -1.0
        symbol_gain = 1.0 + scenario.gain_ripple * math.sin(symbol_index / 7.0)
        if (
            scenario.fade_start is not None
            and scenario.fade_stop is not None
            and scenario.fade_start <= symbol_index < scenario.fade_stop
        ):
            symbol_gain *= scenario.fade_scale

        phase_radians = math.radians(
            scenario.phase_ripple_degrees * math.sin(symbol_index / 11.0)
        )
        i_symbol = symbol * symbol_gain * math.cos(phase_radians)
        q_symbol = symbol * symbol_gain * math.sin(phase_radians)
        if (
            scenario.corruption_mode == "invert"
            and scenario.corruption_start_symbol is not None
            and scenario.corruption_stop_symbol is not None
            and scenario.corruption_start_symbol <= symbol_index < scenario.corruption_stop_symbol
        ):
            i_symbol *= -1.0
            q_symbol *= -1.0
        for _ in range(SAMPLES_PER_SYMBOL):
            i_val = i_symbol + scenario.i_dc + rng.gauss(0.0, scenario.i_noise_stddev)
            q_val = q_symbol + scenario.q_dc + rng.gauss(0.0, scenario.q_noise_stddev)
            samples.append(struct.pack("<ff", i_val, q_val))
    return b"".join(samples)


def write_metadata(path: Path, frame_bits: list[int], scenario: ScenarioConfig) -> None:
    payload = MESSAGE + bytes([crc8_bytes(MESSAGE)])
    metadata = {
        "scenario": scenario.name,
        "description": scenario.description,
        "message": MESSAGE.decode("ascii"),
        "message_bytes": len(MESSAGE),
        "payload_bytes_with_crc": len(payload),
        "sync_word_bits": SYNC_WORD,
        "samples_per_symbol": SAMPLES_PER_SYMBOL,
        "coded_bits_per_frame": len(frame_bits) - len(SYNC_WORD),
        "seed": scenario.seed,
        "i_noise_stddev": scenario.i_noise_stddev,
        "q_noise_stddev": scenario.q_noise_stddev,
        "gain_ripple": scenario.gain_ripple,
        "fade_start_symbol": scenario.fade_start,
        "fade_stop_symbol": scenario.fade_stop,
        "fade_scale": scenario.fade_scale,
        "phase_ripple_degrees": scenario.phase_ripple_degrees,
        "corruption_start_symbol": scenario.corruption_start_symbol,
        "corruption_stop_symbol": scenario.corruption_stop_symbol,
        "corruption_mode": scenario.corruption_mode,
    }
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def default_output_paths(profile: str) -> tuple[Path, Path]:
    base = Path("data/synthetic/canned_replay")
    if profile == "healthy":
        return base / "demo_conv_bpsk.iq", base / "demo_conv_bpsk.json"
    if profile == "impaired":
        return base / "demo_conv_bpsk_impaired.iq", base / "demo_conv_bpsk_impaired.json"
    if profile == "failed":
        return base / "demo_conv_bpsk_failed.iq", base / "demo_conv_bpsk_failed.json"
    raise ValueError(f"Unsupported profile {profile!r}")


def generate_profile(output: Path, metadata: Path, scenario: ScenarioConfig) -> None:
    frame_bits = build_frame_bits()
    iq_bytes = modulate_bpsk(frame_bits, scenario)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(iq_bytes)
    metadata.parent.mkdir(parents=True, exist_ok=True)
    write_metadata(metadata, frame_bits, scenario)

    print(f"Wrote {output}")
    print(f"Wrote {metadata}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--profile",
        choices=["healthy", "impaired", "failed", "all"],
        default="all",
        help="Which deterministic replay scenario to generate",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Path to the generated float32 IQ file for single-profile output",
    )
    parser.add_argument(
        "--metadata",
        type=Path,
        default=None,
        help="Path to the metadata JSON companion file for single-profile output",
    )
    args = parser.parse_args()

    if args.profile == "all":
        if args.output is not None or args.metadata is not None:
            parser.error("--output and --metadata may only be used with a single profile")
        for profile in ("healthy", "impaired", "failed"):
            output_path, metadata_path = default_output_paths(profile)
            generate_profile(output_path, metadata_path, SCENARIOS[profile])
        return

    scenario = SCENARIOS[args.profile]
    output_path = args.output
    metadata_path = args.metadata
    if output_path is None or metadata_path is None:
        default_output, default_metadata = default_output_paths(args.profile)
        output_path = output_path or default_output
        metadata_path = metadata_path or default_metadata

    generate_profile(output_path, metadata_path, scenario)


if __name__ == "__main__":
    main()
