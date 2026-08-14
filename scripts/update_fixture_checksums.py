#!/usr/bin/env python3
"""Write or verify the tracked SHA-256 manifest for synthetic IQ fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT_DIR / "data/synthetic"
MANIFEST_PATH = DATA_DIR / "fixture_manifest.json"
FIXTURE_DIRECTORIES = (
    DATA_DIR / "acquisition",
    DATA_DIR / "canned_replay",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fixture_paths() -> list[Path]:
    paths = []
    for directory in FIXTURE_DIRECTORIES:
        if not directory.is_dir():
            raise FileNotFoundError(f"fixture directory not found: {directory}")
        paths.extend(directory.glob("*.iq"))
        paths.extend(directory.glob("*.json"))
    return sorted(paths, key=lambda path: path.relative_to(ROOT_DIR).as_posix())


def generator_records(paths: list[Path]) -> list[dict[str, object]]:
    grouped: dict[str, dict[str, object]] = {}
    for path in paths:
        if path.suffix != ".json":
            continue
        metadata = json.loads(path.read_text(encoding="utf-8"))
        generator = metadata.get("generator")
        scenario = metadata.get("scenario")
        seed = metadata.get("seed")
        preamble_seed = metadata.get("preamble_seed")
        if not isinstance(generator, str) or not generator:
            raise ValueError(f"{path} does not record its generator")
        if not isinstance(scenario, str) or not isinstance(seed, int):
            raise ValueError(f"{path} does not record a scenario seed")
        record = grouped.setdefault(
            generator,
            {
                "path": generator,
                "preamble_seed": preamble_seed,
                "fixture_seeds": {},
            },
        )
        if record["preamble_seed"] != preamble_seed:
            raise ValueError(f"inconsistent preamble seed for {generator}")
        fixture_seeds = record["fixture_seeds"]
        if not isinstance(fixture_seeds, dict):
            raise ValueError("internal fixture seed record is malformed")
        fixture_seeds[scenario] = seed
    return [grouped[key] for key in sorted(grouped)]


def create_manifest() -> dict[str, object]:
    paths = fixture_paths()
    return {
        "schema": "satcom-fec-trust-lab/fixture-manifest-v1",
        "hash_algorithm": "sha256",
        "generated_by": "scripts/update_fixture_checksums.py",
        "generators": generator_records(paths),
        "files": [
            {
                "path": path.relative_to(ROOT_DIR).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
            for path in paths
        ],
    }


def serialized_manifest(manifest: dict[str, object]) -> str:
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the tracked manifest instead of rewriting it",
    )
    arguments = parser.parse_args()

    try:
        expected = serialized_manifest(create_manifest())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: cannot calculate fixture checksums: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        if not MANIFEST_PATH.is_file():
            print(f"error: fixture manifest not found: {MANIFEST_PATH}", file=sys.stderr)
            return 1
        actual = MANIFEST_PATH.read_text(encoding="utf-8")
        if actual != expected:
            print(
                "error: synthetic fixture hashes or recorded seeds do not match "
                f"{MANIFEST_PATH.relative_to(ROOT_DIR)}",
                file=sys.stderr,
            )
            print(
                "       regenerate fixtures and run scripts/update_fixture_checksums.py",
                file=sys.stderr,
            )
            return 1
        manifest = json.loads(actual)
        print(
            "fixture integrity passed: "
            f"{len(manifest['files'])} files, {len(manifest['generators'])} generators"
        )
        return 0

    MANIFEST_PATH.write_text(expected, encoding="utf-8")
    print(f"wrote {MANIFEST_PATH.relative_to(ROOT_DIR)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
