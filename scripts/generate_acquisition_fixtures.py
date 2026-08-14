#!/usr/bin/env python3
"""Generate deterministic complex-IQ fixtures for reference acquisition tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import struct
from dataclasses import asdict, dataclass, replace
from pathlib import Path
from typing import Iterable


SAMPLE_RATE_HZ = 100_000.0
SAMPLE_COUNT = 4096
PREAMBLE_LENGTH = 256
TIMING_SEARCH_START = 0
TIMING_SEARCH_STOP_INCLUSIVE = SAMPLE_COUNT - PREAMBLE_LENGTH
TIMING_SEARCH_STEP = 1
CFO_HYPOTHESES_HZ = (
    -2000.0,
    -1500.0,
    -1000.0,
    -500.0,
    0.0,
    500.0,
    1000.0,
    1500.0,
    2000.0,
)
DEFAULT_PREAMBLE_SEED = 2029


@dataclass(frozen=True)
class Distractor:
    amplitude: float
    phase_radians: float
    timing_offset: int
    cfo_hz: float


@dataclass(frozen=True)
class Scenario:
    name: str
    description: str
    seed: int
    noise_sigma_per_component: float
    amplitude: float
    phase_radians: float
    timing_offset: int
    cfo_hz: float
    fade_depth: float = 0.0
    distractor: Distractor | None = None


SCENARIOS = (
    Scenario(
        name="clean",
        description="High-SNR preamble with zero carrier-frequency offset.",
        seed=3101,
        noise_sigma_per_component=0.02,
        amplitude=1.0,
        phase_radians=0.25,
        timing_offset=768,
        cfo_hz=0.0,
    ),
    Scenario(
        name="noisy",
        description="Preamble at lower SNR with a negative carrier-frequency offset.",
        seed=3102,
        noise_sigma_per_component=0.24,
        amplitude=1.0,
        phase_radians=-0.6,
        timing_offset=1320,
        cfo_hz=-500.0,
    ),
    Scenario(
        name="frequency_offset",
        description="Preamble centered on a non-zero frequency hypothesis.",
        seed=3103,
        noise_sigma_per_component=0.06,
        amplitude=0.9,
        phase_radians=1.1,
        timing_offset=1024,
        cfo_hz=1500.0,
    ),
    Scenario(
        name="ambiguous",
        description="Primary preamble plus a weaker delayed preamble at another frequency.",
        seed=3104,
        noise_sigma_per_component=0.08,
        amplitude=1.0,
        phase_radians=0.45,
        timing_offset=900,
        cfo_hz=-500.0,
        distractor=Distractor(
            amplitude=0.82,
            phase_radians=-1.0,
            timing_offset=2600,
            cfo_hz=1000.0,
        ),
    ),
    Scenario(
        name="weak_faded",
        description="Weak preamble with deterministic intra-preamble amplitude fading.",
        seed=3105,
        noise_sigma_per_component=0.13,
        amplitude=0.48,
        phase_radians=-0.35,
        timing_offset=1700,
        cfo_hz=500.0,
        fade_depth=0.6,
    ),
)


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


def complex_gaussian(rng: random.Random, sigma: float) -> complex:
    # Box-Muller keeps fixture generation independent of external numeric packages.
    u1 = max(rng.random(), 1e-15)
    u2 = rng.random()
    radius = sigma * math.sqrt(-2.0 * math.log(u1))
    angle = 2.0 * math.pi * u2
    return complex(radius * math.cos(angle), radius * math.sin(angle))


def fading_envelope(length: int, depth: float) -> list[float]:
    if depth <= 0.0:
        return [1.0] * length
    return [
        1.0 - depth * 0.5 * (1.0 - math.cos(2.0 * math.pi * n / (length - 1)))
        for n in range(length)
    ]


def inject_preamble(
    capture: list[complex],
    preamble: list[complex],
    *,
    amplitude: float,
    phase_radians: float,
    timing_offset: int,
    cfo_hz: float,
    fade_depth: float = 0.0,
) -> None:
    envelope = fading_envelope(len(preamble), fade_depth)
    for index, preamble_sample in enumerate(preamble):
        phase = phase_radians + 2.0 * math.pi * cfo_hz * index / SAMPLE_RATE_HZ
        carrier = complex(math.cos(phase), math.sin(phase))
        capture[timing_offset + index] += (
            amplitude * envelope[index] * preamble_sample * carrier
        )


def validate_scenario(scenario: Scenario) -> None:
    if (
        not math.isfinite(scenario.noise_sigma_per_component)
        or scenario.noise_sigma_per_component < 0.0
    ):
        raise ValueError("noise sigma must be non-negative")
    if not math.isfinite(scenario.amplitude) or scenario.amplitude <= 0.0:
        raise ValueError("amplitude must be greater than zero")
    if not math.isfinite(scenario.phase_radians):
        raise ValueError("phase must be finite")
    if not math.isfinite(scenario.fade_depth) or not 0.0 <= scenario.fade_depth < 1.0:
        raise ValueError("fade depth must be in [0, 1)")
    if not TIMING_SEARCH_START <= scenario.timing_offset <= TIMING_SEARCH_STOP_INCLUSIVE:
        raise ValueError("timing offset is outside the configured search range")
    if scenario.cfo_hz not in CFO_HYPOTHESES_HZ:
        raise ValueError("carrier-frequency offset must be one of the search hypotheses")
    if scenario.distractor is not None:
        distractor = scenario.distractor
        if not math.isfinite(distractor.amplitude) or distractor.amplitude <= 0.0:
            raise ValueError("distractor amplitude must be greater than zero")
        if not math.isfinite(distractor.phase_radians):
            raise ValueError("distractor phase must be finite")
        if not (
            TIMING_SEARCH_START
            <= distractor.timing_offset
            <= TIMING_SEARCH_STOP_INCLUSIVE
        ):
            raise ValueError("distractor timing offset is outside the search range")
        if distractor.cfo_hz not in CFO_HYPOTHESES_HZ:
            raise ValueError("distractor CFO must be one of the search hypotheses")


def make_capture(scenario: Scenario, preamble: list[complex]) -> list[complex]:
    validate_scenario(scenario)
    rng = random.Random(scenario.seed)
    capture = [
        complex_gaussian(rng, scenario.noise_sigma_per_component)
        for _ in range(SAMPLE_COUNT)
    ]
    inject_preamble(
        capture,
        preamble,
        amplitude=scenario.amplitude,
        phase_radians=scenario.phase_radians,
        timing_offset=scenario.timing_offset,
        cfo_hz=scenario.cfo_hz,
        fade_depth=scenario.fade_depth,
    )
    if scenario.distractor is not None:
        inject_preamble(
            capture,
            preamble,
            amplitude=scenario.distractor.amplitude,
            phase_radians=scenario.distractor.phase_radians,
            timing_offset=scenario.distractor.timing_offset,
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


def write_fixture(
    output_dir: Path,
    scenario: Scenario,
    preamble: list[complex],
    preamble_seed: int,
    preamble_sha256: str,
) -> None:
    capture = make_capture(scenario, preamble)
    iq_path = output_dir / f"{scenario.name}.iq"
    write_iq(iq_path, capture)

    metadata = {
        "schema": "satcom-fec-trust-lab/acquisition-fixture-v1",
        "generator": "scripts/generate_acquisition_fixtures.py",
        "scenario": scenario.name,
        "description": scenario.description,
        "iq_format": "interleaved little-endian float32 I,Q",
        "seed": scenario.seed,
        "sample_count": SAMPLE_COUNT,
        "sample_rate_hz": SAMPLE_RATE_HZ,
        "preamble_file": "preamble_qpsk_256.iq",
        "preamble_length": PREAMBLE_LENGTH,
        "preamble_modulation": "deterministic QPSK",
        "preamble_seed": preamble_seed,
        "preamble_sha256": preamble_sha256,
        "iq_sha256": sha256_file(iq_path),
        "timing_search_start": TIMING_SEARCH_START,
        "timing_search_stop_inclusive": TIMING_SEARCH_STOP_INCLUSIVE,
        "timing_search_step": TIMING_SEARCH_STEP,
        "cfo_hypotheses_hz": list(CFO_HYPOTHESES_HZ),
        "true_timing_offset": scenario.timing_offset,
        "true_cfo_hz": scenario.cfo_hz,
        "amplitude": scenario.amplitude,
        "phase_radians": scenario.phase_radians,
        "noise_sigma_per_component": scenario.noise_sigma_per_component,
        "fade_depth": scenario.fade_depth,
        "distractor": asdict(scenario.distractor) if scenario.distractor else None,
    }
    (output_dir / f"{scenario.name}.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("data/synthetic/acquisition"),
        help="directory for generated IQ and metadata files",
    )
    parser.add_argument(
        "--scenario",
        choices=("all", *(scenario.name for scenario in SCENARIOS)),
        default="all",
        help="generate all standard fixtures or one selected scenario",
    )
    parser.add_argument("--preamble-seed", type=int, default=DEFAULT_PREAMBLE_SEED)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--noise-sigma", type=float)
    parser.add_argument("--amplitude", type=float)
    parser.add_argument("--phase-radians", type=float)
    parser.add_argument("--timing-offset", type=int)
    parser.add_argument("--cfo-hz", type=float)
    parser.add_argument("--fade-depth", type=float)
    parser.add_argument("--distractor-amplitude", type=float)
    parser.add_argument("--distractor-phase-radians", type=float)
    parser.add_argument("--distractor-timing-offset", type=int)
    parser.add_argument("--distractor-cfo-hz", type=float)
    return parser.parse_args()


def apply_overrides(scenario: Scenario, arguments: argparse.Namespace) -> Scenario:
    override_names = (
        "seed",
        "noise_sigma",
        "amplitude",
        "phase_radians",
        "timing_offset",
        "cfo_hz",
        "fade_depth",
        "distractor_amplitude",
        "distractor_phase_radians",
        "distractor_timing_offset",
        "distractor_cfo_hz",
    )
    if arguments.scenario == "all" and any(
        getattr(arguments, name) is not None for name in override_names
    ):
        raise ValueError("signal overrides require selecting one --scenario")

    replacements = {}
    field_map = {
        "seed": "seed",
        "noise_sigma": "noise_sigma_per_component",
        "amplitude": "amplitude",
        "phase_radians": "phase_radians",
        "timing_offset": "timing_offset",
        "cfo_hz": "cfo_hz",
        "fade_depth": "fade_depth",
    }
    for argument_name, field_name in field_map.items():
        value = getattr(arguments, argument_name)
        if value is not None:
            replacements[field_name] = value

    distractor_override_names = (
        "distractor_amplitude",
        "distractor_phase_radians",
        "distractor_timing_offset",
        "distractor_cfo_hz",
    )
    if any(getattr(arguments, name) is not None for name in distractor_override_names):
        if arguments.distractor_amplitude == 0.0:
            replacements["distractor"] = None
        else:
            existing = scenario.distractor
            amplitude = arguments.distractor_amplitude
            phase_radians = arguments.distractor_phase_radians
            timing_offset = arguments.distractor_timing_offset
            cfo_hz = arguments.distractor_cfo_hz
            if existing is not None:
                amplitude = existing.amplitude if amplitude is None else amplitude
                phase_radians = (
                    existing.phase_radians if phase_radians is None else phase_radians
                )
                timing_offset = (
                    existing.timing_offset if timing_offset is None else timing_offset
                )
                cfo_hz = existing.cfo_hz if cfo_hz is None else cfo_hz
            if amplitude is None or timing_offset is None or cfo_hz is None:
                raise ValueError(
                    "a new distractor requires amplitude, timing offset, and CFO"
                )
            replacements["distractor"] = Distractor(
                amplitude=amplitude,
                phase_radians=0.0 if phase_radians is None else phase_radians,
                timing_offset=timing_offset,
                cfo_hz=cfo_hz,
            )
    return replace(scenario, **replacements)


def main() -> int:
    arguments = parse_arguments()
    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    selected = [
        scenario
        for scenario in SCENARIOS
        if arguments.scenario in ("all", scenario.name)
    ]
    selected = [apply_overrides(scenario, arguments) for scenario in selected]

    preamble = qpsk_preamble(PREAMBLE_LENGTH, arguments.preamble_seed)
    preamble_path = arguments.output_dir / "preamble_qpsk_256.iq"
    write_iq(preamble_path, preamble)
    preamble_sha256 = sha256_file(preamble_path)
    for scenario in selected:
        write_fixture(
            arguments.output_dir,
            scenario,
            preamble,
            arguments.preamble_seed,
            preamble_sha256,
        )

    print(
        f"generated {len(selected)} acquisition fixture(s) in {arguments.output_dir} "
        f"with preamble_seed={arguments.preamble_seed}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
