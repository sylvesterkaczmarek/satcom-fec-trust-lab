#!/usr/bin/env python3

"""Run independent acquisition benchmark processes and summarize run medians."""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parents[1]
BENCHMARK_BINARY = ROOT_DIR / "build/host_replay/benchmark_acquisition"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Run at least five independent benchmark processes, preserve every "
            "JSON report, and summarize the distribution of run medians."
        )
    )
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/acquisition-repeatability"),
    )
    parser.add_argument("--workload", action="append", default=[])
    parser.add_argument("--warmup-rounds", type=int, default=2)
    parser.add_argument("--samples", type=int, default=15)
    parser.add_argument("--min-sample-ms", type=float, default=50.0)
    parser.add_argument("--seed", default="0x534154434F4D4645")
    parser.add_argument("--skip-build", action="store_true")
    arguments = parser.parse_args()
    if arguments.runs < 5:
        parser.error("--runs must be at least 5")
    if arguments.warmup_rounds < 0:
        parser.error("--warmup-rounds must be non-negative")
    if arguments.samples < 3:
        parser.error("--samples must be at least 3")
    if arguments.min_sample_ms <= 0.0:
        parser.error("--min-sample-ms must be positive")
    return arguments


def benchmark_arguments(arguments: argparse.Namespace) -> list[str]:
    command = [
        str(BENCHMARK_BINARY),
        "--warmup-rounds",
        str(arguments.warmup_rounds),
        "--samples",
        str(arguments.samples),
        "--min-sample-ms",
        str(arguments.min_sample_ms),
        "--seed",
        arguments.seed,
    ]
    for workload in arguments.workload:
        command.extend(("--workload", workload))
    return command


def mode_lookup(report: dict[str, Any]) -> dict[tuple[str, str, str], dict[str, Any]]:
    modes: dict[tuple[str, str, str], dict[str, Any]] = {}
    for workload in report["workloads"]:
        for implementation in workload["implementations"]:
            for mode in implementation["modes"]:
                key = (
                    workload["name"],
                    mode["name"],
                    implementation["requested_implementation"],
                )
                modes[key] = mode
    return modes


def nullable_round(value: float | None, digits: int = 9) -> float | None:
    return None if value is None else round(value, digits)


def summarize_reports(reports: list[dict[str, Any]]) -> dict[str, Any]:
    first_modes = mode_lookup(reports[0])
    expected_keys = set(first_modes)
    source_commits = {
        report["benchmark"]["git_commit_sha"] for report in reports
    }
    dirty_states = {
        report["benchmark"]["git_working_tree_dirty_at_build"]
        for report in reports
    }
    workload_definitions = {
        json.dumps(
            {workload["name"]: workload["definition"] for workload in report["workloads"]},
            sort_keys=True,
        )
        for report in reports
    }
    host_metadata = {
        json.dumps(report["host"], sort_keys=True) for report in reports
    }
    build_metadata = {
        json.dumps(report["build"], sort_keys=True) for report in reports
    }
    runtime_metadata = {
        json.dumps(report["runtime_cpu_features"], sort_keys=True)
        for report in reports
    }
    if len(source_commits) != 1 or len(dirty_states) != 1:
        raise RuntimeError("benchmark source metadata changed between process runs")
    if len(workload_definitions) != 1:
        raise RuntimeError("workload definitions changed between process runs")
    if len(host_metadata) != 1 or len(build_metadata) != 1 or len(runtime_metadata) != 1:
        raise RuntimeError("host, build, or runtime feature metadata changed between runs")

    lookups = [mode_lookup(report) for report in reports]
    if any(set(lookup) != expected_keys for lookup in lookups):
        raise RuntimeError("implementation/mode set changed between process runs")

    groups: list[dict[str, Any]] = []
    for workload, mode_name, implementation in sorted(expected_keys):
        run_medians: list[dict[str, Any]] = []
        median_values: list[float] = []
        sme2_speedups: list[dict[str, Any]] = []
        for run_index, lookup in enumerate(lookups, start=1):
            mode = lookup[(workload, mode_name, implementation)]
            median = (
                mode["timing"]["latency_ms"]["median"]
                if mode["valid"]
                else None
            )
            run_medians.append(
                {
                    "run": run_index,
                    "median_latency_ms": median,
                    "valid": mode["valid"],
                }
            )
            if median is not None:
                median_values.append(float(median))
            if implementation == "sme2":
                sme2_speedups.append(
                    {
                        "run": run_index,
                        "speedup_vs_neon": mode["speedup_vs_neon"],
                    }
                )

        mean = statistics.fmean(median_values) if median_values else None
        coefficient_of_variation = None
        if mean and len(median_values) > 1:
            coefficient_of_variation = statistics.stdev(median_values) / mean
        minimum = min(median_values) if median_values else None
        maximum = max(median_values) if median_values else None
        groups.append(
            {
                "workload": workload,
                "mode": mode_name,
                "implementation": implementation,
                "valid_run_count": len(median_values),
                "run_medians": run_medians,
                "median_of_run_medians_ms": nullable_round(
                    statistics.median(median_values) if median_values else None
                ),
                "minimum_run_median_ms": nullable_round(minimum),
                "maximum_run_median_ms": nullable_round(maximum),
                "run_median_spread_ms": nullable_round(
                    maximum - minimum
                    if minimum is not None and maximum is not None
                    else None
                ),
                "run_median_coefficient_of_variation": nullable_round(
                    coefficient_of_variation
                ),
                "sme2_speedup_vs_neon_by_run": sme2_speedups,
            }
        )

    return {
        "schema_version": 1,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "run_count": len(reports),
        "benchmark_source_commit": next(iter(source_commits)),
        "git_working_tree_dirty_at_build": next(iter(dirty_states)),
        "host": reports[0]["host"],
        "build": reports[0]["build"],
        "runtime_cpu_features": reports[0]["runtime_cpu_features"],
        "workload_definitions": {
            workload["name"]: workload["definition"]
            for workload in reports[0]["workloads"]
        },
        "limitations": [
            "Runs are independent processes on one host.",
            "No CPU affinity, frequency locking, thermal stabilization, or energy measurement is performed.",
            "Raw per-process reports are authoritative and are not averaged away.",
        ],
        "groups": groups,
    }


def print_compact_summary(summary: dict[str, Any]) -> None:
    print(
        "workload       mode             implementation  "
        "median-of-medians ms   min        max        CV"
    )
    for group in summary["groups"]:
        median = group["median_of_run_medians_ms"]
        if median is None:
            continue
        coefficient = group["run_median_coefficient_of_variation"]
        coefficient_text = "n/a" if coefficient is None else f"{coefficient:.6f}"
        print(
            f"{group['workload']:<14} {group['mode']:<16} "
            f"{group['implementation']:<15} {median:>20.6f} "
            f"{group['minimum_run_median_ms']:>10.6f} "
            f"{group['maximum_run_median_ms']:>10.6f} "
            f"{coefficient_text:>9}"
        )
        if group["implementation"] == "sme2":
            speedups = [
                entry["speedup_vs_neon"]
                for entry in group["sme2_speedup_vs_neon_by_run"]
            ]
            print(f"  SME2/NEON speedup by run: {speedups}")


def main() -> int:
    arguments = parse_arguments()
    if not arguments.skip_build:
        subprocess.run(
            ("bash", "scripts/build_host_tools.sh", "benchmark_acquisition"),
            cwd=ROOT_DIR,
            check=True,
        )

    output_directory = (
        arguments.output_dir
        if arguments.output_dir.is_absolute()
        else ROOT_DIR / arguments.output_dir
    )
    output_directory.mkdir(parents=True, exist_ok=True)
    command = benchmark_arguments(arguments)
    reports: list[dict[str, Any]] = []
    run_entries: list[dict[str, Any]] = []

    for run_index in range(1, arguments.runs + 1):
        completed = subprocess.run(
            command,
            cwd=ROOT_DIR,
            check=False,
            capture_output=True,
            text=True,
        )
        run_name = f"run-{run_index:02d}.json"
        run_path = output_directory / run_name
        run_path.write_text(completed.stdout, encoding="utf-8")
        if completed.stderr:
            (output_directory / f"run-{run_index:02d}.stderr.txt").write_text(
                completed.stderr, encoding="utf-8"
            )
        if completed.returncode != 0:
            raise RuntimeError(
                f"benchmark process {run_index} failed with exit code "
                f"{completed.returncode}; output preserved at {run_path}"
            )
        report = json.loads(completed.stdout)
        if not report.get("ok"):
            raise RuntimeError(f"benchmark process {run_index} reported ok=false")
        reports.append(report)
        run_entries.append(
            {
                "run": run_index,
                "path": run_name,
                "timestamp_utc": report["benchmark"]["timestamp_utc"],
                "git_commit_sha": report["benchmark"]["git_commit_sha"],
                "git_working_tree_dirty_at_build": report["benchmark"][
                    "git_working_tree_dirty_at_build"
                ],
            }
        )
        print(f"completed independent process run {run_index}/{arguments.runs}")

    summary = summarize_reports(reports)
    summary["benchmark_command"] = [
        "build/host_replay/benchmark_acquisition",
        *command[1:],
    ]
    summary["runs"] = run_entries
    summary_path = output_directory / "summary.json"
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )
    print_compact_summary(summary)
    print(f"raw reports and summary: {output_directory}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (json.JSONDecodeError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
