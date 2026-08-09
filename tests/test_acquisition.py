import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
FIXTURE_DIR = ROOT_DIR / "data/synthetic/acquisition"
GOLDEN_PATH = ROOT_DIR / "tests/golden/acquisition_clean.json"
ACQUISITION_BINARY = ROOT_DIR / "build/host_replay/acquisition_demo"
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
        cls.neon_available = run_acquisition("clean")["neon_kernel_compiled"]

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


if __name__ == "__main__":
    unittest.main()
