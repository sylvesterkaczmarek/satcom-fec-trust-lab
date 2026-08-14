import csv
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT_DIR / "data/synthetic/acquisition"
GOLDEN_PATH = ROOT_DIR / "tests/golden/acquisition_clean.json"
ACQUISITION_BINARY = ROOT_DIR / "build/host_replay/acquisition_demo"
BENCHMARK_BINARY = ROOT_DIR / "build/host_replay/benchmark_acquisition"
FIXTURE_NAMES = ("clean", "noisy", "frequency_offset", "ambiguous", "weak_faded")
SCORE_RELATIVE_TOLERANCE = 2.0e-4
SCORE_ABSOLUTE_TOLERANCE = 1.0e-3


def run_acquisition(
    fixture_name: str,
    implementation: str = "reference",
    *,
    check: bool = True,
) -> dict:
    completed = subprocess.run(
        (
            str(ACQUISITION_BINARY),
            "--iq",
            str(FIXTURE_DIR / f"{fixture_name}.iq"),
            "--metadata",
            str(FIXTURE_DIR / f"{fixture_name}.json"),
            "--implementation",
            implementation,
        ),
        cwd=ROOT_DIR,
        check=check,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def assert_subset(test_case: unittest.TestCase, actual: dict, expected: dict) -> None:
    for key, expected_value in expected.items():
        test_case.assertIn(key, actual)
        test_case.assertEqual(actual[key], expected_value)


class AcquisitionReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(
            ("bash", "scripts/build_host_tools.sh", "acquisition_demo"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )
        implementation_status = run_acquisition("clean")
        cls.neon_available = implementation_status["neon_kernel_compiled"]
        cls.sme2_compiled = implementation_status["sme2_kernel_compiled"]
        cls.sme2_available = (
            cls.sme2_compiled
            and implementation_status["sme2_runtime_supported"]
        )

    def test_clean_fixture_matches_golden_output(self) -> None:
        result = run_acquisition("clean")
        golden = json.loads(GOLDEN_PATH.read_text(encoding="utf-8"))

        assert_subset(self, result, golden)
        self.assertGreater(result["best_score"], result["second_best_score"])
        self.assertGreater(result["peak_ratio"], 1.0)
        self.assertGreater(result["normalized_peak_separation"], 0.0)

    def test_all_impaired_fixtures_recover_ground_truth(self) -> None:
        for fixture_name in FIXTURE_NAMES[1:]:
            with self.subTest(fixture=fixture_name):
                metadata = json.loads(
                    (FIXTURE_DIR / f"{fixture_name}.json").read_text(encoding="utf-8")
                )
                result = run_acquisition(fixture_name)

                self.assertTrue(result["ok"])
                self.assertTrue(result["acquisition_success"])
                self.assertEqual(
                    result["detected_timing_offset"], metadata["true_timing_offset"]
                )
                self.assertEqual(result["detected_cfo_hz"], metadata["true_cfo_hz"])
                self.assertEqual(result["implementation"], "reference")
                self.assertEqual(result["requested_implementation"], "reference")
                self.assertEqual(
                    result["evaluated_candidate_count"],
                    result["timing_hypothesis_count"]
                    * result["cfo_hypothesis_count"],
                )

    def test_ambiguous_fixture_exposes_the_distractor_as_runner_up(self) -> None:
        clean = run_acquisition("clean")
        ambiguous = run_acquisition("ambiguous")
        metadata = json.loads(
            (FIXTURE_DIR / "ambiguous.json").read_text(encoding="utf-8")
        )
        distractor = metadata["distractor"]

        self.assertEqual(
            ambiguous["second_best_timing_offset"], distractor["timing_offset"]
        )
        self.assertEqual(ambiguous["second_best_cfo_hz"], distractor["cfo_hz"])
        self.assertLess(ambiguous["peak_ratio"], clean["peak_ratio"])
        self.assertLess(
            ambiguous["normalized_peak_separation"],
            clean["normalized_peak_separation"],
        )

    def test_generator_is_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            generated_directories = (Path(first), Path(second))
            for output_directory in generated_directories:
                subprocess.run(
                    (
                        "python3",
                        "scripts/generate_acquisition_fixtures.py",
                        "--output-dir",
                        str(output_directory),
                    ),
                    cwd=ROOT_DIR,
                    check=True,
                    capture_output=True,
                    text=True,
                )

            expected_names = sorted(
                path.name
                for path in FIXTURE_DIR.iterdir()
                if path.suffix in {".iq", ".json"}
            )
            for output_directory in generated_directories:
                actual_names = sorted(path.name for path in output_directory.iterdir())
                self.assertEqual(actual_names, expected_names)

            for filename in expected_names:
                checked_in = (FIXTURE_DIR / filename).read_bytes()
                first_bytes = (generated_directories[0] / filename).read_bytes()
                second_bytes = (generated_directories[1] / filename).read_bytes()
                self.assertEqual(first_bytes, second_bytes)
                self.assertEqual(first_bytes, checked_in)

    def test_duplicate_cfo_hypothesis_is_rejected(self) -> None:
        metadata = json.loads(
            (FIXTURE_DIR / "clean.json").read_text(encoding="utf-8")
        )
        metadata["preamble_file"] = str(FIXTURE_DIR / "preamble_qpsk_256.iq")
        metadata["cfo_hypotheses_hz"].append(0.0)

        with tempfile.TemporaryDirectory() as temporary_directory:
            metadata_path = Path(temporary_directory) / "invalid.json"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            completed = subprocess.run(
                (
                    str(ACQUISITION_BINARY),
                    "--iq",
                    str(FIXTURE_DIR / "clean.iq"),
                    "--metadata",
                    str(metadata_path),
                ),
                cwd=ROOT_DIR,
                check=False,
                capture_output=True,
                text=True,
            )

        result = json.loads(completed.stdout)
        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(result["ok"])
        self.assertEqual(
            result["error"],
            "frequency-offset hypotheses must not contain duplicates",
        )

    def test_neon_fixture_results_align_with_reference(self) -> None:
        if not self.neon_available:
            self.skipTest("NEON acquisition execution requires a native Arm64 build")

        for fixture_name in FIXTURE_NAMES:
            with self.subTest(fixture=fixture_name):
                reference = run_acquisition(fixture_name, "reference")
                neon = run_acquisition(fixture_name, "neon")

                self.assertTrue(neon["ok"])
                self.assertTrue(neon["acquisition_success"])
                self.assertTrue(neon["neon_kernel_compiled"])
                self.assertEqual(neon["requested_implementation"], "neon")
                self.assertEqual(neon["implementation"], "neon")
                self.assertEqual(
                    neon["detected_timing_offset"],
                    reference["detected_timing_offset"],
                )
                self.assertEqual(neon["detected_cfo_hz"], reference["detected_cfo_hz"])
                self.assertEqual(
                    neon["second_best_timing_offset"],
                    reference["second_best_timing_offset"],
                )
                self.assertEqual(
                    neon["second_best_cfo_hz"],
                    reference["second_best_cfo_hz"],
                )
                for score_name in ("best_score", "second_best_score"):
                    tolerance = (
                        SCORE_ABSOLUTE_TOLERANCE
                        + SCORE_RELATIVE_TOLERANCE * abs(reference[score_name])
                    )
                    self.assertAlmostEqual(
                        neon[score_name],
                        reference[score_name],
                        delta=tolerance,
                    )

    def test_neon_request_never_silently_falls_back(self) -> None:
        result = run_acquisition("clean", "neon", check=self.neon_available)
        if self.neon_available:
            self.assertTrue(result["ok"])
            self.assertEqual(result["implementation"], "neon")
        else:
            self.assertFalse(result["ok"])
            self.assertEqual(result["requested_implementation"], "neon")
            self.assertEqual(result["implementation"], "unavailable")
            self.assertFalse(result["neon_kernel_compiled"])
            self.assertEqual(
                result["error"],
                "NEON acquisition kernel is not compiled for this target",
            )

    def test_direct_kernel_equivalence_report(self) -> None:
        completed = subprocess.run(
            ("bash", "scripts/check_acquisition_neon.sh"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )
        report = json.loads(completed.stdout)

        self.assertTrue(report["ok"])
        self.assertEqual(report["neon_kernel_compiled"], self.neon_available)
        self.assertEqual(report["neon_executed"], self.neon_available)
        if self.neon_available:
            self.assertEqual(report["implementation"], "neon")
            self.assertGreaterEqual(len(report["cases"]), 10)
            for case in report["cases"]:
                self.assertTrue(case["candidate_identity_match"])
                self.assertTrue(case["within_tolerance"])
                self.assertLessEqual(
                    case["correlation_real_difference"],
                    case["correlation_component_tolerance"],
                )
                self.assertLessEqual(
                    case["correlation_imag_difference"],
                    case["correlation_component_tolerance"],
                )
                self.assertLessEqual(
                    case["score_difference"],
                    case["score_tolerance"],
                )
        else:
            self.assertEqual(report["implementation"], "unavailable")
            self.assertEqual(report["cases"], [])


class NeonDisassemblyVerifierTests(unittest.TestCase):
    def run_verifier(
        self, neon_text: str, reference_text: str
    ) -> subprocess.CompletedProcess:
        with tempfile.TemporaryDirectory() as temporary_directory:
            neon_path = Path(temporary_directory) / "neon.txt"
            reference_path = Path(temporary_directory) / "reference.txt"
            neon_path.write_text(neon_text, encoding="utf-8")
            reference_path.write_text(reference_text, encoding="utf-8")
            return subprocess.run(
                (
                    "bash",
                    "scripts/check_neon_disassembly.sh",
                    str(neon_path),
                    str(reference_path),
                ),
                cwd=ROOT_DIR,
                check=False,
                capture_output=True,
                text=True,
            )

    def test_accepts_gnu_ld2_operand_lane_syntax(self) -> None:
        completed = self.run_verifier(
            """
  138:\t4cdf8820 \tld2\t{v0.4s-v1.4s}, [x1], #32
  140:\t6e22dc05 \tfmul\tv5.4s, v0.4s, v2.4s
  150:\t4eb0d4a1 \tfsub\tv1.4s, v5.4s, v16.4s
  1c8:\t4ea7cca1 \tfmls\tv1.4s, v5.4s, v7.4s
""",
            "  20: 1e620800 fmul d0, d0, d2\n",
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("AArch64 LD2 structure load", completed.stdout)
        self.assertIn("ld2", completed.stdout)

    def test_accepts_load_plus_uzp_deinterleave_lowering(self) -> None:
        completed = self.run_verifier(
            """
  100: 3dc00020 ldr q0, [x1]
  104: 3dc00421 ldr q1, [x1, #16]
  108: 4e811802 uzp1 v2.4s, v0.4s, v1.4s
  10c: 4e815803 uzp2 v3.4s, v0.4s, v1.4s
  110: 6e22dc44 fmul v4.4s, v2.4s, v2.4s
  114: 4e23cc84 fmla v4.4s, v4.4s, v3.4s
""",
            "  20: 1e620800 fmul d0, d0, d2\n",
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("vector load(s) followed by UZP1/UZP2", completed.stdout)
        self.assertIn("uzp1", completed.stdout.lower())

    def test_rejects_symbol_only_or_scalar_evidence(self) -> None:
        completed = self.run_verifier(
            """
00000000 <run_neon_acquisition>:
  20: 1e220800 fmul s0, s0, s2
  24: 1e222800 fadd s0, s0, s2
""",
            "  20: 1e620800 fmul d0, d0, d2\n",
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("deinterleave", completed.stderr)

    def test_rejects_equivalent_vector_sequence_in_reference_object(self) -> None:
        vector_sequence = """
  100: 4cdf8820 ld2 {v0.4s-v1.4s}, [x1], #32
  104: 6e22dc44 fmul v4.4s, v2.4s, v2.4s
  108: 4e23cc84 fmla v4.4s, v4.4s, v3.4s
"""
        completed = self.run_verifier(vector_sequence, vector_sequence)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("scalar reference object", completed.stderr)


class AcquisitionBenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(
            ("bash", "scripts/build_host_tools.sh", "benchmark_acquisition"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )

    def test_small_benchmark_report_is_correctness_gated_and_structured(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            json_path = Path(temporary_directory) / "report.json"
            csv_path = Path(temporary_directory) / "report.csv"
            completed = subprocess.run(
                (
                    str(BENCHMARK_BINARY),
                    "--workload",
                    "small",
                    "--warmup-rounds",
                    "0",
                    "--samples",
                    "3",
                    "--min-sample-ms",
                    "1",
                    "--json",
                    str(json_path),
                    "--csv",
                    str(csv_path),
                ),
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(completed.stdout)
            self.assertEqual(
                report,
                json.loads(json_path.read_text(encoding="utf-8")),
            )
            with csv_path.open(encoding="utf-8", newline="") as csv_file:
                csv_rows = list(csv.DictReader(csv_file))

            repeated = subprocess.run(
                (
                    str(BENCHMARK_BINARY),
                    "--workload",
                    "small",
                    "--warmup-rounds",
                    "0",
                    "--samples",
                    "3",
                    "--min-sample-ms",
                    "1",
                ),
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
                text=True,
            )
            repeated_report = json.loads(repeated.stdout)

        self.assertTrue(report["ok"])
        self.assertEqual(report["schema_version"], 2)
        self.assertEqual(report["benchmark"]["name"], "acquisition-workload-sweep")
        self.assertEqual(report["benchmark"]["timed_sample_count"], 3)
        self.assertEqual(report["benchmark"]["minimum_sample_duration_ms"], 1.0)
        self.assertEqual(report["build"]["cxx_standard"], "C++17")
        self.assertIn("reference", report["build"]["source_compile_flags"])
        self.assertIn("neon", report["build"]["source_compile_flags"])
        self.assertIn("sme2", report["build"]["source_compile_flags"])
        if report["runtime_cpu_features"]["sme2_kernel_compiled"]:
            neon_flags = report["build"]["source_compile_flags"]["neon"]
            self.assertTrue(
                ("nosve" in neon_flags and "nosme" in neon_flags)
                or "armv8-a" in neon_flags
            )
        self.assertIn("cpu_model_source", report["host"])
        self.assertIn("device_model", report["host"])
        self.assertEqual(len(report["workloads"]), 1)

        workload = report["workloads"][0]
        self.assertEqual(workload["name"], "small")
        self.assertEqual(
            workload["definition"],
            {
                "iq_sample_count": 2048,
                "preamble_length": 64,
                "timing_hypothesis_count": 1024,
                "cfo_hypothesis_count": 5,
                "candidate_correlation_count": 5120,
                "complex_mac_count": 327680,
                "sample_rate_hz": 48000.0,
                "cfo_spacing_hz": 250.0,
            },
        )
        self.assertTrue(workload["reference_result"]["valid"])
        self.assertEqual(workload["fixture"]["per_capture_window_count"], 2)
        self.assertTrue(
            workload["fairness_checks"][
                "accelerated_weight_tables_bitwise_equal"
            ]
        )
        self.assertTrue(
            workload["fairness_checks"]["per_capture_reference_cases_valid"]
        )
        self.assertEqual(
            workload["memory_accounting"]["common_input_capture_payload_bytes"],
            2048 * 8,
        )
        self.assertEqual(len(workload["execution_order_by_sample"]), 3)
        self.assertEqual(len(workload["implementations"]), 3)
        repeated_workload = repeated_report["workloads"][0]
        self.assertEqual(workload["fixture"], repeated_workload["fixture"])
        self.assertEqual(
            workload["reference_result"],
            repeated_workload["reference_result"],
        )
        self.assertEqual(
            workload["execution_order_by_sample"],
            repeated_workload["execution_order_by_sample"],
        )

        expected_tasks = set()
        for implementation in workload["implementations"]:
            self.assertEqual(len(implementation["modes"]), 3)
            self.assertGreater(
                implementation["memory_bytes"]["reusable_plan_payload"], 0
            )
            if implementation["requested_implementation"] == "sme2":
                self.assertEqual(
                    implementation["memory_bytes"][
                        "per_capture_workspace_payload"
                    ],
                    2 * 64 * 1024 * 4,
                )
                self.assertEqual(
                    implementation["memory_bytes"]["correlation_output_payload"],
                    2 * 5 * 1024 * 4,
                )
            else:
                self.assertEqual(
                    implementation["memory_bytes"][
                        "total_temporary_workspace_payload"
                    ],
                    0,
                )
            if implementation["available"]:
                self.assertTrue(implementation["executed"])
                self.assertTrue(implementation["correctness"]["passed"])
                self.assertEqual(
                    implementation["correctness"]["per_capture_case_count"], 2
                )
                self.assertEqual(
                    implementation["correctness"]["per_capture_cases_passed"],
                    2,
                )
                self.assertEqual(
                    implementation["correctness"]["actual_implementation"],
                    implementation["requested_implementation"],
                )
                for mode in implementation["modes"]:
                    self.assertTrue(mode["valid"])
                    self.assertIsInstance(mode["timing"], dict)
                    self.assertEqual(mode["timing"]["sample_count"], 3)
                    self.assertEqual(
                        len(mode["timing"]["per_sample_latency_ms"]), 3
                    )
                    self.assertGreater(mode["timing"]["latency_ms"]["median"], 0.0)
                    self.assertGreater(
                        mode["timing"]["candidate_correlations_per_second"], 0.0
                    )
                    expected_tasks.add(
                        f'{implementation["requested_implementation"]}/{mode["name"]}'
                    )
            else:
                self.assertFalse(implementation["executed"])
                self.assertNotEqual(implementation["unavailable_reason"], "")
                for mode in implementation["modes"]:
                    self.assertFalse(mode["valid"])
                    self.assertIsNone(mode["timing"])
                    self.assertNotEqual(mode["error"], "")

        for execution_order in workload["execution_order_by_sample"]:
            self.assertEqual(set(execution_order), expected_tasks)
            self.assertEqual(len(execution_order), len(expected_tasks))

        self.assertEqual(len(csv_rows), 9)
        self.assertEqual(
            {(row["implementation"], row["mode"]) for row in csv_rows},
            {
                ("reference", "steady-state"),
                ("reference", "per-capture"),
                ("reference", "setup-inclusive"),
                ("neon", "steady-state"),
                ("neon", "per-capture"),
                ("neon", "setup-inclusive"),
                ("sme2", "steady-state"),
                ("sme2", "per-capture"),
                ("sme2", "setup-inclusive"),
            },
        )

    def test_repeatability_helper_preserves_five_process_reports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            completed = subprocess.run(
                (
                    "python3",
                    "scripts/repeat_acquisition_benchmark.py",
                    "--runs",
                    "5",
                    "--output-dir",
                    temporary_directory,
                    "--workload",
                    "small",
                    "--warmup-rounds",
                    "0",
                    "--samples",
                    "3",
                    "--min-sample-ms",
                    "1",
                    "--skip-build",
                ),
                cwd=ROOT_DIR,
                check=True,
                capture_output=True,
                text=True,
            )
            output_directory = Path(temporary_directory)
            reports = [
                json.loads(
                    (output_directory / f"run-{index:02d}.json").read_text(
                        encoding="utf-8"
                    )
                )
                for index in range(1, 6)
            ]
            summary = json.loads(
                (output_directory / "summary.json").read_text(encoding="utf-8")
            )

        self.assertIn("completed independent process run 5/5", completed.stdout)
        self.assertEqual(len(reports), 5)
        self.assertTrue(all(report["ok"] for report in reports))
        self.assertEqual(summary["run_count"], 5)
        self.assertEqual(len(summary["runs"]), 5)
        self.assertEqual(len(summary["groups"]), 9)
        reference_per_capture = next(
            group
            for group in summary["groups"]
            if group["workload"] == "small"
            and group["mode"] == "per-capture"
            and group["implementation"] == "reference"
        )
        self.assertEqual(reference_per_capture["valid_run_count"], 5)
        self.assertEqual(len(reference_per_capture["run_medians"]), 5)
        self.assertIsNotNone(
            reference_per_capture["median_of_run_medians_ms"]
        )
        sme2_per_capture = next(
            group
            for group in summary["groups"]
            if group["workload"] == "small"
            and group["mode"] == "per-capture"
            and group["implementation"] == "sme2"
        )
        self.assertEqual(len(sme2_per_capture["sme2_speedup_vs_neon_by_run"]), 5)


class AcquisitionSme2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        subprocess.run(
            ("bash", "scripts/build_host_tools.sh", "acquisition_demo"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )
        implementation_status = run_acquisition("clean")
        cls.neon_available = implementation_status["neon_kernel_compiled"]
        cls.sme2_compiled = implementation_status["sme2_kernel_compiled"]
        cls.sme2_available = (
            cls.sme2_compiled
            and implementation_status["sme2_runtime_supported"]
        )

    def test_sme2_fixture_results_align_with_reference_and_neon(self) -> None:
        if not self.sme2_available:
            self.skipTest("SME2 acquisition execution requires an SME2 build and host")

        for fixture_name in FIXTURE_NAMES:
            with self.subTest(fixture=fixture_name):
                reference = run_acquisition(fixture_name, "reference")
                sme2 = run_acquisition(fixture_name, "sme2")

                self.assertTrue(sme2["ok"])
                self.assertTrue(sme2["acquisition_success"])
                self.assertTrue(sme2["sme2_kernel_compiled"])
                self.assertTrue(sme2["sme2_runtime_supported"])
                self.assertEqual(sme2["requested_implementation"], "sme2")
                self.assertEqual(sme2["implementation"], "sme2")
                self.assertEqual(sme2["implementation_class"], "sme2-za-vgx4")
                self.assertEqual(sme2["sme2_mechanism"], "za-vgx4-fmla-fmls")
                self.assertEqual(
                    sme2["detected_timing_offset"],
                    reference["detected_timing_offset"],
                )
                self.assertEqual(sme2["detected_cfo_hz"], reference["detected_cfo_hz"])
                self.assertEqual(
                    sme2["second_best_timing_offset"],
                    reference["second_best_timing_offset"],
                )
                self.assertEqual(
                    sme2["second_best_cfo_hz"],
                    reference["second_best_cfo_hz"],
                )
                for score_name in ("best_score", "second_best_score"):
                    tolerance = (
                        SCORE_ABSOLUTE_TOLERANCE
                        + SCORE_RELATIVE_TOLERANCE * abs(reference[score_name])
                    )
                    self.assertAlmostEqual(
                        sme2[score_name],
                        reference[score_name],
                        delta=tolerance,
                    )

                if self.neon_available:
                    neon = run_acquisition(fixture_name, "neon")
                    for field in (
                        "detected_timing_offset",
                        "detected_cfo_hz",
                        "second_best_timing_offset",
                        "second_best_cfo_hz",
                    ):
                        self.assertEqual(sme2[field], neon[field])

    def test_sme2_request_never_silently_falls_back(self) -> None:
        result = run_acquisition("clean", "sme2", check=self.sme2_available)
        if self.sme2_available:
            self.assertTrue(result["ok"])
            self.assertEqual(result["implementation"], "sme2")
            self.assertEqual(result["sme2_mechanism"], "za-vgx4-fmla-fmls")
        else:
            self.assertFalse(result["ok"])
            self.assertEqual(result["requested_implementation"], "sme2")
            self.assertEqual(result["implementation"], "unavailable")
            expected_error = (
                "SME2 acquisition kernel is compiled but SME2 execution is unavailable on this host"
                if self.sme2_compiled
                else "SME2 acquisition kernel is not compiled for this target"
            )
            self.assertEqual(result["error"], expected_error)

    def test_direct_sme2_equivalence_report(self) -> None:
        completed = subprocess.run(
            ("bash", "scripts/check_sme2_acquisition.sh"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )
        report = json.loads(completed.stdout)

        self.assertTrue(report["ok"])
        self.assertEqual(report["sme2_kernel_compiled"], self.sme2_compiled)
        self.assertEqual(report["sme2_executed"], self.sme2_available)
        if self.sme2_available:
            self.assertEqual(report["implementation"], "sme2")
            self.assertEqual(report["mechanism"], "za-vgx4-fmla-fmls")
            self.assertGreaterEqual(len(report["cases"]), 7)
            self.assertTrue(any(case["preamble_length"] % 2 for case in report["cases"]))
            self.assertTrue(
                any(
                    case["timing_count"] > report["timing_batch_width"]
                    and case["timing_count"] % report["timing_batch_width"] != 0
                    for case in report["cases"]
                )
            )
            self.assertTrue(any(case["frequency_count"] > 4 for case in report["cases"]))
            for case in report["cases"]:
                self.assertTrue(case["all_correlations_within_tolerance"])
                self.assertTrue(case["best_candidate_match"])
                self.assertTrue(case["second_best_candidate_match"])
                self.assertTrue(case["three_path_candidate_identity_match"])
                self.assertTrue(case["passed"])
        else:
            self.assertEqual(report["implementation"], "unavailable")
            self.assertEqual(report["cases"], [])


if __name__ == "__main__":
    unittest.main()
