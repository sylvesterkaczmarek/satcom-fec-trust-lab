#!/usr/bin/env python3
"""Verify that architecture flags stay confined to their implementation files."""

from __future__ import annotations

import argparse
import json
import shlex
import sys
from pathlib import Path


REFERENCE_SOURCE = "src/acquisition/acquisition_reference.cpp"
NEON_SOURCE = "src/acquisition/acquisition_neon.cpp"
SME2_SOURCE = "src/acquisition/acquisition_sme2.cpp"
FEC_NEON_SOURCE = "src/fec/viterbi_decoder_neon.cpp"
FEC_STREAMING_VECTOR_SOURCE = "src/fec/branch_metrics_streaming_vector.cpp"
GENERIC_FEC_SOURCE = "src/fec/convolutional_codec.cpp"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--expect",
        choices=("portable", "neon", "sme2"),
        required=True,
        help="requested CMake architecture mode",
    )
    return parser.parse_args()


def source_key(entry: dict[str, object], root: Path) -> str:
    source = Path(str(entry["file"]))
    if not source.is_absolute():
        source = Path(str(entry.get("directory", root))) / source
    try:
        return source.resolve().relative_to(root).as_posix()
    except ValueError:
        return source.resolve().as_posix()


def command_tokens(entry: dict[str, object]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list):
        return [str(argument) for argument in arguments]
    command = entry.get("command")
    if not isinstance(command, str):
        raise ValueError("compile command has neither arguments nor command")
    return shlex.split(command)


def architecture_flags(tokens: list[str]) -> list[str]:
    return [
        token
        for token in tokens
        if token.startswith(("-march=", "-mcpu=", "--target="))
        or token in ("-arch", "arm64", "aarch64")
    ]


def has_definition(tokens: list[str], name: str) -> bool:
    return any(token == f"-D{name}" or token.startswith(f"-D{name}=") for token in tokens)


def is_sme2_target_flag(token: str) -> bool:
    if not token.startswith(("-march=", "-mcpu=")):
        return False
    features = token.lower().split("=", 1)[-1].split("+")[1:]
    return "sme2" in features


def is_scalable_target_flag(token: str) -> bool:
    if not token.startswith(("-march=", "-mcpu=")):
        return False
    features = token.lower().split("=", 1)[-1].split("+")[1:]
    return any(feature in ("sve", "sve2", "sme", "sme2") for feature in features)


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    arguments = parse_arguments()
    root = Path(__file__).resolve().parents[1]
    database_path = arguments.build_dir / "compile_commands.json"
    if not database_path.is_file():
        fail(f"compile command database not found: {database_path}")

    try:
        entries = json.loads(database_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as error:
        fail(f"cannot read {database_path}: {error}")
    if not isinstance(entries, list):
        fail("compile_commands.json must contain an array")

    commands: dict[str, list[str]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or "file" not in entry:
            fail("compile command entry is malformed")
        key = source_key(entry, root)
        commands.setdefault(key, command_tokens(entry))

    for required_source in (REFERENCE_SOURCE, NEON_SOURCE, SME2_SOURCE):
        if required_source not in commands:
            fail(f"missing compile command for {required_source}")

    reference_tokens = commands[REFERENCE_SOURCE]
    reference_arch = architecture_flags(reference_tokens)
    if any(is_scalable_target_flag(token) for token in reference_arch):
        fail("scalar acquisition reference received an SVE/SME target flag")
    if has_definition(reference_tokens, "SATCOMFEC_ACQUISITION_NEON_COMPILED") or has_definition(
        reference_tokens, "SATCOMFEC_ACQUISITION_SME2_COMPILED"
    ):
        fail("scalar acquisition reference received an accelerated-kernel definition")
    if not any(
        token in ("-fno-vectorize", "-fno-tree-vectorize")
        for token in reference_tokens
    ):
        fail("scalar acquisition reference is missing loop-vectorization control")
    if not any(
        token in ("-fno-slp-vectorize", "-fno-tree-slp-vectorize")
        for token in reference_tokens
    ):
        fail("scalar acquisition reference is missing SLP-vectorization control")

    neon_tokens = commands[NEON_SOURCE]
    sme2_tokens = commands[SME2_SOURCE]
    if any(is_sme2_target_flag(token) for token in architecture_flags(neon_tokens)):
        fail("NEON acquisition source received an SME2 target flag")

    sme2_target_sources = {
        source
        for source, tokens in commands.items()
        if any(is_sme2_target_flag(token) for token in architecture_flags(tokens))
    }
    allowed_sme2_sources = {SME2_SOURCE, FEC_STREAMING_VECTOR_SOURCE}
    unexpected_sme2_sources = sorted(sme2_target_sources - allowed_sme2_sources)
    if unexpected_sme2_sources:
        fail(
            "SME2 target flag contaminated: " + ", ".join(unexpected_sme2_sources)
        )

    if arguments.expect == "sme2":
        if not any(is_sme2_target_flag(token) for token in architecture_flags(sme2_tokens)):
            fail("SME2 acquisition source has no SME2 target flag")
        if not has_definition(sme2_tokens, "SATCOMFEC_ACQUISITION_SME2_COMPILED"):
            fail("SME2 acquisition source has no compiled-kernel definition")
        if SME2_SOURCE not in sme2_target_sources:
            fail("SME2 acquisition source was not identified as SME2-targeted")
        streaming_tokens = commands.get(FEC_STREAMING_VECTOR_SOURCE, [])
        if streaming_tokens and not has_definition(
            streaming_tokens, "SATCOMFEC_FEC_STREAMING_VECTOR_COMPILED"
        ):
            fail("legacy streaming-vector FEC source has no compiled-kernel definition")
        if not has_definition(neon_tokens, "SATCOMFEC_ACQUISITION_NEON_COMPILED"):
            fail("SME2 comparison build did not compile the NEON acquisition kernel")
        neon_arch = architecture_flags(neon_tokens)
        if any(is_scalable_target_flag(token) for token in neon_arch):
            fail("NEON comparison target enables scalable-vector or matrix extensions")
    else:
        if sme2_target_sources:
            fail(f"{arguments.expect} build unexpectedly contains SME2-targeted sources")
        if has_definition(sme2_tokens, "SATCOMFEC_ACQUISITION_SME2_COMPILED"):
            fail(f"{arguments.expect} build labels the SME2 acquisition kernel compiled")

    if arguments.expect == "neon" and not has_definition(
        neon_tokens, "SATCOMFEC_ACQUISITION_NEON_COMPILED"
    ):
        fail("explicit NEON build did not compile the NEON acquisition kernel")

    for source in (GENERIC_FEC_SOURCE,):
        if source in commands and any(
            is_scalable_target_flag(token)
            for token in architecture_flags(commands[source])
        ):
            fail(f"generic scalar FEC source received an SVE/SME flag: {source}")

    evidence = {}
    for source in (
        REFERENCE_SOURCE,
        NEON_SOURCE,
        SME2_SOURCE,
        FEC_NEON_SOURCE,
        FEC_STREAMING_VECTOR_SOURCE,
        GENERIC_FEC_SOURCE,
    ):
        if source not in commands:
            continue
        tokens = commands[source]
        evidence[source] = {
            "architecture_flags": architecture_flags(tokens),
            "neon_kernel_definition": has_definition(
                tokens, "SATCOMFEC_ACQUISITION_NEON_COMPILED"
            ) or has_definition(tokens, "SATCOMFEC_FEC_NEON_COMPILED"),
            "sme2_kernel_definition": has_definition(
                tokens, "SATCOMFEC_ACQUISITION_SME2_COMPILED"
            ),
            "streaming_vector_definition": has_definition(
                tokens, "SATCOMFEC_FEC_STREAMING_VECTOR_COMPILED"
            ),
        }

    print(
        json.dumps(
            {
                "ok": True,
                "mode": arguments.expect,
                "compile_commands": str(database_path),
                "translation_units": evidence,
            },
            indent=2,
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
