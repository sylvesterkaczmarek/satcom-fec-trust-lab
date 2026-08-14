#!/usr/bin/env python3
"""Generate deterministic end-to-end IQ replay fixtures and metadata."""

from __future__ import annotations

import argparse
from dataclasses import asdict, dataclass
import hashlib
import json
import math
import os
import random
import struct
from pathlib import Path
from typing import Iterable


SYNC_WORD = [1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 1]
MESSAGE = b"SATCOM DEMO OK"
SAMPLES_PER_SYMBOL = 8
SAMPLE_RATE_HZ = 48_000.0
SAMPLE_COUNT = 4096
PREAMBLE_LENGTH = 256
PREAMBLE_SEED = 2029
CFO_HYPOTHESES_HZ = (-500.0, -250.0, 0.0, 250.0, 500.0)


@dataclass(frozen=True)
class Distractor:
    amplitude: float
    phase_radians: float
    timing_offset: int
    cfo_hz: float


@dataclass(frozen=True)
class ScenarioConfig:
    name: str
    seed: int
    signal_present: bool
    timing_offset: int | None
    cfo_hz: float | None
    carrier_phase_radians: float
    amplitude: float
    preamble_amplitude_scale: float
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
    distractor: Distractor | None = None
    acquisition_expected: bool = True
    decode_expected: bool = True
    description: str = ""


SCENARIOS = {
    "healthy": ScenarioConfig(
        name="healthy",
        seed=7,
        signal_present=True,
        timing_offset=192,
        cfo_hz=250.0,
        carrier_phase_radians=0.35,
        amplitude=1.0,
        preamble_amplitude_scale=1.0,
        i_dc=0.03,
        q_dc=-0.02,
        i_noise_stddev=0.04,
        q_noise_stddev=0.02,
        gain_ripple=0.05,
        description=(
            "Decodable replay with leading noise, non-zero timing offset, and "
            "a positive carrier-frequency offset."
        ),
    ),
    "impaired": ScenarioConfig(
        name="impaired",
        seed=29,
        signal_present=True,
        timing_offset=384,
        cfo_hz=-250.0,
        carrier_phase_radians=-0.70,
        amplitude=1.0,
        preamble_amplitude_scale=0.65,
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
            "Decodable replay with a weaker preamble, added noise, amplitude "
            "ripple, and a short frame fade."
        ),
    ),
    "ambiguous": ScenarioConfig(
        name="ambiguous",
        seed=37,
        signal_present=True,
        timing_offset=1500,
        cfo_hz=0.0,
        carrier_phase_radians=0.50,
        amplitude=1.0,
        preamble_amplitude_scale=1.0,
        i_dc=0.02,
        q_dc=-0.01,
        i_noise_stddev=0.06,
        q_noise_stddev=0.04,
        gain_ripple=0.08,
        distractor=Distractor(
            amplitude=0.94,
            phase_radians=-0.90,
            timing_offset=300,
            cfo_hz=-500.0,
        ),
        description=(
            "Decodable replay with a competing preamble that lowers peak "
            "separation without changing the transmitted payload."
        ),
    ),
    "failed": ScenarioConfig(
        name="failed",
        seed=41,
        signal_present=True,
        timing_offset=256,
        cfo_hz=500.0,
        carrier_phase_radians=0.90,
        amplitude=1.0,
        preamble_amplitude_scale=0.9,
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
        decode_expected=False,
        description=(
            "Acquirable replay with deliberately corrupted coded data that "
            "reaches CRC rejection."
        ),
    ),
    "no_signal": ScenarioConfig(
        name="no_signal",
        seed=53,
        signal_present=False,
        timing_offset=None,
        cfo_hz=None,
        carrier_phase_radians=0.0,
        amplitude=0.0,
        preamble_amplitude_scale=0.0,
        i_dc=0.03,
        q_dc=-0.02,
        i_noise_stddev=0.16,
        q_noise_stddev=0.16,
        gain_ripple=0.0,
        acquisition_expected=False,
        decode_expected=False,
        description=(
            "Noise-only capture used to verify acquisition rejection before "
            "demodulation and decoding."
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
    return [
        (value >> bit) & 1
        for value in data
        for bit in range(7, -1, -1)
    ]


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


def qpsk_preamble(length: int, seed: int) -> list[complex]:
    rng = random.Random(seed)
    scale = 1.0 / math.sqrt(2.0)
    return [
        complex(
            scale if rng.getrandbits(1) else -scale,
            scale if rng.getrandbits(1) else -scale,
        )
        for _ in range(length)
    ]


def modulate_frame(frame_bits: list[int], scenario: ScenarioConfig) -> list[complex]:
    samples: list[complex] = []
    for symbol_index, bit in enumerate(frame_bits):
        symbol = 1.0 if bit else -1.0
        gain = 1.0 + scenario.gain_ripple * math.sin(symbol_index / 7.0)
        if (
            scenario.fade_start is not None
            and scenario.fade_stop is not None
            and scenario.fade_start <= symbol_index < scenario.fade_stop
        ):
            gain *= scenario.fade_scale

        phase = math.radians(
            scenario.phase_ripple_degrees * math.sin(symbol_index / 11.0)
        )
        sample = symbol * gain * complex(math.cos(phase), math.sin(phase))
        if (
            scenario.corruption_mode == "invert"
            and scenario.corruption_start_symbol is not None
            and scenario.corruption_stop_symbol is not None
            and scenario.corruption_start_symbol
            <= symbol_index
            < scenario.corruption_stop_symbol
        ):
            sample *= -1.0
        samples.extend([sample] * SAMPLES_PER_SYMBOL)
    return samples


def inject_waveform(
    capture: list[complex],
    waveform: Iterable[complex],
    *,
    timing_offset: int,
    amplitude: float,
    phase_radians: float,
    cfo_hz: float,
) -> None:
    for index, waveform_sample in enumerate(waveform):
        phase = phase_radians + 2.0 * math.pi * cfo_hz * index / SAMPLE_RATE_HZ
        carrier = complex(math.cos(phase), math.sin(phase))
        capture[timing_offset + index] += amplitude * waveform_sample * carrier


def validate_scenario(scenario: ScenarioConfig, transmission_length: int) -> None:
    if not scenario.signal_present:
        if scenario.timing_offset is not None or scenario.cfo_hz is not None:
            raise ValueError("noise-only scenarios must not define signal ground truth")
        return
    if scenario.timing_offset is None or scenario.cfo_hz is None:
        raise ValueError("signal scenarios require timing and CFO ground truth")
    if scenario.cfo_hz not in CFO_HYPOTHESES_HZ:
        raise ValueError("scenario CFO must be one of the configured hypotheses")
    if scenario.timing_offset < 0 or (
        scenario.timing_offset + transmission_length > SAMPLE_COUNT
    ):
        raise ValueError("scenario transmission does not fit in the capture")
    if scenario.amplitude <= 0.0 or scenario.preamble_amplitude_scale <= 0.0:
        raise ValueError("signal amplitudes must be positive")
    if scenario.distractor is not None:
        distractor = scenario.distractor
        if distractor.cfo_hz not in CFO_HYPOTHESES_HZ:
            raise ValueError("distractor CFO must be a configured hypothesis")
        if distractor.timing_offset < 0 or (
            distractor.timing_offset + PREAMBLE_LENGTH > SAMPLE_COUNT
        ):
            raise ValueError("distractor preamble does not fit in the capture")


def make_capture(
    scenario: ScenarioConfig,
    preamble: list[complex],
    frame_samples: list[complex],
) -> list[complex]:
    transmission_length = len(preamble) + len(frame_samples)
    validate_scenario(scenario, transmission_length)
    rng = random.Random(scenario.seed)
    capture = [
        complex(
            scenario.i_dc + rng.gauss(0.0, scenario.i_noise_stddev),
            scenario.q_dc + rng.gauss(0.0, scenario.q_noise_stddev),
        )
        for _ in range(SAMPLE_COUNT)
    ]

    if scenario.signal_present:
        assert scenario.timing_offset is not None
        assert scenario.cfo_hz is not None
        transmission = [
            scenario.preamble_amplitude_scale * sample for sample in preamble
        ] + frame_samples
        inject_waveform(
            capture,
            transmission,
            timing_offset=scenario.timing_offset,
            amplitude=scenario.amplitude,
            phase_radians=scenario.carrier_phase_radians,
            cfo_hz=scenario.cfo_hz,
        )

    if scenario.distractor is not None:
        inject_waveform(
            capture,
            preamble,
            timing_offset=scenario.distractor.timing_offset,
            amplitude=scenario.distractor.amplitude,
            phase_radians=scenario.distractor.phase_radians,
            cfo_hz=scenario.distractor.cfo_hz,
        )
    return capture


def write_iq(path: Path, samples: Iterable[complex]) -> None:
    with path.open("wb") as output:
        for sample in samples:
            output.write(struct.pack("<ff", sample.real, sample.imag))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_metadata(
    path: Path,
    iq_path: Path,
    preamble_path: Path,
    frame_bits: list[int],
    scenario: ScenarioConfig,
) -> None:
    payload = MESSAGE + bytes([crc8_bytes(MESSAGE)])
    frame_sample_count = len(frame_bits) * SAMPLES_PER_SYMBOL
    timing_search_stop = SAMPLE_COUNT - PREAMBLE_LENGTH - frame_sample_count
    trailing_noise_samples = None
    if scenario.signal_present and scenario.timing_offset is not None:
        trailing_noise_samples = (
            SAMPLE_COUNT
            - scenario.timing_offset
            - PREAMBLE_LENGTH
            - frame_sample_count
        )

    preamble_reference = os.path.relpath(preamble_path, start=path.parent)
    metadata = {
        "schema": "satcom-fec-trust-lab/replay-fixture-v2",
        "generator": "scripts/generate_synthetic_iq.py",
        "scenario": scenario.name,
        "description": scenario.description,
        "iq_format": "interleaved little-endian float32 I,Q",
        "message": MESSAGE.decode("ascii"),
        "message_bytes": len(MESSAGE),
        "payload_bytes_with_crc": len(payload),
        "sync_word_bits": SYNC_WORD,
        "coded_bits_per_frame": len(frame_bits) - len(SYNC_WORD),
        "frame_symbol_count": len(frame_bits),
        "frame_sample_count": frame_sample_count,
        "samples_per_symbol": SAMPLES_PER_SYMBOL,
        "sample_count": SAMPLE_COUNT,
        "sample_rate_hz": SAMPLE_RATE_HZ,
        "preamble_file": preamble_reference,
        "preamble_length": PREAMBLE_LENGTH,
        "preamble_modulation": "deterministic QPSK",
        "preamble_seed": PREAMBLE_SEED,
        "preamble_sha256": sha256_file(preamble_path),
        "iq_sha256": sha256_file(iq_path),
        "timing_search_start": 0,
        "timing_search_stop_inclusive": timing_search_stop,
        "timing_search_step": 1,
        "cfo_hypotheses_hz": list(CFO_HYPOTHESES_HZ),
        "signal_present": scenario.signal_present,
        "true_timing_offset": scenario.timing_offset,
        "true_cfo_hz": scenario.cfo_hz,
        "leading_noise_samples": scenario.timing_offset,
        "trailing_noise_samples": trailing_noise_samples,
        "acquisition_expected": scenario.acquisition_expected,
        "decode_expected": scenario.decode_expected,
        "seed": scenario.seed,
        "amplitude": scenario.amplitude,
        "preamble_amplitude_scale": scenario.preamble_amplitude_scale,
        "carrier_phase_radians": scenario.carrier_phase_radians,
        "i_dc": scenario.i_dc,
        "q_dc": scenario.q_dc,
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
        "distractor": asdict(scenario.distractor) if scenario.distractor else None,
    }
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def output_stem(profile: str) -> str:
    if profile == "healthy":
        return "demo_conv_bpsk"
    return f"demo_conv_bpsk_{profile}"


def default_output_paths(profile: str) -> tuple[Path, Path]:
    base = Path("data/synthetic/canned_replay")
    stem = output_stem(profile)
    return base / f"{stem}.iq", base / f"{stem}.json"


def generate_profile(
    output: Path,
    metadata: Path,
    preamble_path: Path,
    preamble: list[complex],
    frame_bits: list[int],
    scenario: ScenarioConfig,
) -> None:
    frame_samples = modulate_frame(frame_bits, scenario)
    capture = make_capture(scenario, preamble, frame_samples)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_iq(output, capture)
    metadata.parent.mkdir(parents=True, exist_ok=True)
    write_metadata(metadata, output, preamble_path, frame_bits, scenario)
    print(f"Wrote {output}")
    print(f"Wrote {metadata}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--profile",
        choices=[*SCENARIOS, "all"],
        default="all",
        help="deterministic replay scenario to generate",
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--preamble-output", type=Path)
    args = parser.parse_args()

    if args.profile == "all" and (args.output is not None or args.metadata is not None):
        parser.error("--output and --metadata require a single --profile")

    base = Path("data/synthetic/canned_replay")
    preamble_path = args.preamble_output or base / "preamble_qpsk_256.iq"
    preamble_path.parent.mkdir(parents=True, exist_ok=True)
    preamble = qpsk_preamble(PREAMBLE_LENGTH, PREAMBLE_SEED)
    write_iq(preamble_path, preamble)
    print(f"Wrote {preamble_path}")

    frame_bits = build_frame_bits()
    profiles = list(SCENARIOS) if args.profile == "all" else [args.profile]
    for profile in profiles:
        default_output, default_metadata = default_output_paths(profile)
        output = args.output or default_output
        metadata = args.metadata or default_metadata
        generate_profile(
            output,
            metadata,
            preamble_path,
            preamble,
            frame_bits,
            SCENARIOS[profile],
        )


if __name__ == "__main__":
    main()
