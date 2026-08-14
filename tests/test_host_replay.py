import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk.json"
IMPAIRED_METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk_impaired.json"
AMBIGUOUS_METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.json"
FAILED_METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk_failed.json"
NO_SIGNAL_METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk_no_signal.json"
GOLDEN_DIR = ROOT_DIR / "tests/golden"


def run_json_command(*args: str, check: bool = True) -> dict:
    completed = subprocess.run(
        args,
        cwd=ROOT_DIR,
        check=check,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def load_golden(name: str) -> dict:
    return json.loads((GOLDEN_DIR / name).read_text(encoding="utf-8"))


def assert_subset(test_case: unittest.TestCase, actual, expected) -> None:
    if isinstance(expected, dict):
        test_case.assertIsInstance(actual, dict)
        for key, expected_value in expected.items():
            test_case.assertIn(key, actual)
            assert_subset(test_case, actual[key], expected_value)
        return
    if isinstance(expected, list):
        test_case.assertEqual(actual, expected)
        return
    test_case.assertEqual(actual, expected)


def assert_decoder_reporting(test_case: unittest.TestCase, result: dict) -> None:
    valid_selections = {
        "viterbi-reference": {"reference"},
        "viterbi-neon": {"neon", "fallback"},
    }
    expected_classes = {
        "reference": "real",
        "neon": "partial",
        "fallback": "fallback",
    }
    decoder = result["decoder"]
    selected = result["branch_metric_implementation"]
    test_case.assertIn(selected, valid_selections[decoder])
    test_case.assertEqual(result["implementation_class"], expected_classes[selected])


class HostReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.metadata = json.loads(METADATA_PATH.read_text(encoding="utf-8"))
        cls.impaired_metadata = json.loads(
            IMPAIRED_METADATA_PATH.read_text(encoding="utf-8")
        )
        cls.ambiguous_metadata = json.loads(
            AMBIGUOUS_METADATA_PATH.read_text(encoding="utf-8")
        )
        cls.failed_metadata = json.loads(
            FAILED_METADATA_PATH.read_text(encoding="utf-8")
        )
        cls.no_signal_metadata = json.loads(
            NO_SIGNAL_METADATA_PATH.read_text(encoding="utf-8")
        )

    def test_canned_replay_decodes_expected_payload(self) -> None:
        result = run_json_command("bash", "scripts/run_replay_demo.sh")
        assert_subset(self, result, load_golden("replay_healthy.json"))
        assert_decoder_reporting(self, result)

        self.assertTrue(result["ok"])
        self.assertEqual(result["decoded_text"], self.metadata["message"])
        self.assertTrue(result["crc_ok"])
        self.assertEqual(result["samples_per_symbol"], self.metadata["samples_per_symbol"])
        self.assertEqual(result["frame_soft_bits"], self.metadata["coded_bits_per_frame"])
        self.assertEqual(result["decoded_payload_bytes"], self.metadata["message_bytes"])
        self.assertEqual(
            result["expected_payload_bytes"],
            self.metadata["message_bytes"],
        )
        self.assertGreater(result["framing"]["sync_score"], 0)
        self.assertFalse(result["framing"]["has_second_best_correlation"])
        self.assertEqual(
            result["acquisition"]["selected_implementation"], "reference"
        )
        self.assertTrue(result["acquisition"]["acquisition_success"])
        self.assertEqual(
            result["acquisition"]["detected_timing_offset"],
            self.metadata["true_timing_offset"],
        )
        self.assertEqual(
            result["acquisition"]["detected_cfo_hz"],
            self.metadata["true_cfo_hz"],
        )
        self.assertEqual(
            result["acquisition"]["ground_truth"]["timing_error_samples"], 0
        )
        self.assertEqual(
            result["acquisition"]["ground_truth"]["cfo_hypothesis_error_hz"], 0
        )
        self.assertGreater(result["acquisition"]["normalized_peak"], 0.9)
        self.assertGreater(
            result["trust_features"]["mean_abs_soft_decision"], 0.0
        )
        self.assertGreaterEqual(result["trust_score"], 0.0)
        self.assertLessEqual(result["trust_score"], 1.0)
        self.assertEqual(result["trust_assessment"]["band"], "high-confidence")
        self.assertAlmostEqual(
            result["trust_score"],
            result["trust_breakdown"]["score"],
            places=6,
        )

    def test_replay_detection_does_not_require_fixture_metadata(self) -> None:
        source_iq = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk.iq"
        with tempfile.TemporaryDirectory() as temporary_directory:
            iq_copy = Path(temporary_directory) / "capture_without_metadata.iq"
            shutil.copyfile(source_iq, iq_copy)
            result = run_json_command(
                "bash",
                "scripts/run_replay_demo.sh",
                str(iq_copy),
                "viterbi-reference",
            )

        self.assertTrue(result["ok"])
        self.assertEqual(result["metadata_path"], "")
        self.assertFalse(result["acquisition"]["ground_truth"]["available"])
        self.assertTrue(result["acquisition"]["acquisition_success"])
        self.assertEqual(
            result["acquisition"]["detected_timing_offset"],
            self.metadata["true_timing_offset"],
        )
        self.assertEqual(
            result["acquisition"]["detected_cfo_hz"],
            self.metadata["true_cfo_hz"],
        )
        self.assertEqual(result["decoded_text"], self.metadata["message"])
        self.assertTrue(result["crc_ok"])

    def test_decoder_entrypoints_align_on_same_prepared_frame(self) -> None:
        result = run_json_command(
            "bash",
            "experiments/viterbi_branch_metrics/run.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk.iq",
            "1",
            "5",
        )
        assert_subset(self, result, load_golden("benchmark_alignment.json"))

        self.assertTrue(result["ok"])
        self.assertTrue(result["outputs_match"])
        self.assertTrue(result["benchmark"]["local_timing_only"])
        self.assertEqual(result["decoded_text"], self.metadata["message"])
        self.assertTrue(result["assumptions"]["same_input_frame"])
        self.assertTrue(result["assumptions"]["same_decoder_settings"])
        self.assertTrue(result["assumptions"]["same_evaluation_window"])
        self.assertTrue(result["assumptions"]["same_traceback_core"])
        self.assertTrue(result["assumptions"]["same_state_machine"])
        self.assertTrue(result["assumptions"]["branch_metric_timed_separately"])
        self.assertTrue(result["assumptions"]["full_decode_includes_branch_metric_preparation"])
        self.assertTrue(result["assumptions"]["same_prepared_soft_bits"])
        self.assertEqual(
            result["prepared_frame"]["frame_length"],
            self.metadata["coded_bits_per_frame"],
        )
        self.assertTrue(result["alignment"]["decoded_bit_count_match"])
        self.assertTrue(result["alignment"]["decoded_bit_checksum_match"])
        self.assertTrue(result["alignment"]["payload_text_match"])

        paths = {path["decoder"]: path for path in result["paths"]}
        expected_neon_class = (
            "partial"
            if paths["viterbi-neon"]["branch_metric"]["selected_implementation"]
            == "neon"
            else "fallback"
        )
        self.assertEqual(
            paths["viterbi-neon"]["implementation_class"],
            expected_neon_class,
        )
        self.assertEqual(paths["viterbi-reference"]["implementation_class"], "real")
        self.assertIn(
            paths["viterbi-streaming-vector"]["implementation_class"],
            {"partial", "fallback"},
        )
        self.assertTrue(paths["viterbi-neon"]["decode_ok"])
        self.assertTrue(paths["viterbi-reference"]["decode_ok"])
        self.assertTrue(paths["viterbi-streaming-vector"]["decode_ok"])
        self.assertIn(
            paths["viterbi-neon"]["branch_metric"]["selected_implementation"],
            {"neon", "fallback"},
        )
        self.assertEqual(
            paths["viterbi-reference"]["branch_metric"]["selected_implementation"],
            "reference",
        )
        self.assertIn(
            paths["viterbi-streaming-vector"]["branch_metric"]
            ["selected_implementation"],
            {"streaming-sve", "fallback"},
        )
        for path in paths.values():
            self.assertGreaterEqual(path["branch_metric"]["elapsed_ms"], 0.0)
            self.assertGreaterEqual(path["branch_metric"]["ns_per_iteration"], 0.0)
            self.assertGreater(path["branch_metric"]["metric_checksum"], 0)
            self.assertGreaterEqual(path["full_decode"]["elapsed_ms"], 0.0)
            self.assertGreaterEqual(path["full_decode"]["ns_per_iteration"], 0.0)
        self.assertEqual(
            paths["viterbi-neon"]["decoded_bit_count"],
            paths["viterbi-reference"]["decoded_bit_count"],
        )
        self.assertEqual(
            paths["viterbi-streaming-vector"]["decoded_bit_count"],
            paths["viterbi-reference"]["decoded_bit_count"],
        )
        self.assertEqual(
            paths["viterbi-neon"]["decoded_bit_checksum"],
            paths["viterbi-reference"]["decoded_bit_checksum"],
        )
        self.assertEqual(
            paths["viterbi-streaming-vector"]["decoded_bit_checksum"],
            paths["viterbi-reference"]["decoded_bit_checksum"],
        )

    def test_end_to_end_acquisition_paths_report_actual_implementation(self) -> None:
        for implementation in ("neon", "sme2"):
            result = run_json_command(
                "bash",
                "scripts/run_replay_demo.sh",
                "--allow-failure",
                "--acquisition",
                implementation,
                "data/synthetic/canned_replay/demo_conv_bpsk.iq",
                "viterbi-reference",
            )
            selected = result["acquisition"]["selected_implementation"]
            self.assertIn(selected, {implementation, "unavailable"})
            if selected == implementation:
                self.assertTrue(result["ok"])
                self.assertEqual(result["decoded_text"], self.metadata["message"])
                self.assertEqual(
                    result["acquisition"]["ground_truth"]["timing_error_samples"],
                    0,
                )
                self.assertEqual(
                    result["acquisition"]["ground_truth"]["cfo_hypothesis_error_hz"],
                    0,
                )
            else:
                self.assertFalse(result["ok"])
                self.assertIn("kernel is not compiled", result["error"])

    def test_branch_metric_paths_match_reference_on_deterministic_inputs(self) -> None:
        result = run_json_command("bash", "scripts/check_branch_metrics.sh")

        self.assertTrue(result["ok"])
        self.assertEqual(result["implementations"]["reference"]["selected"], "reference")
        self.assertIn(result["implementations"]["neon"]["selected"], {"neon", "fallback"})
        self.assertIn(
            result["implementations"]["streaming_vector"]["selected"],
            {"streaming-sve", "fallback"},
        )
        self.assertGreaterEqual(len(result["cases"]), 10)
        for case in result["cases"]:
            self.assertTrue(case["neon_matches_reference"])
            self.assertTrue(case["streaming_vector_matches_reference"])

    def test_impaired_replay_scores_lower_than_healthy(self) -> None:
        healthy = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk.iq",
        )
        impaired = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq",
        )
        assert_subset(self, impaired, load_golden("replay_impaired.json"))
        assert_decoder_reporting(self, impaired)

        self.assertTrue(impaired["ok"])
        self.assertEqual(impaired["decoded_text"], self.impaired_metadata["message"])
        self.assertEqual(
            impaired["samples_per_symbol"], self.impaired_metadata["samples_per_symbol"]
        )
        self.assertEqual(impaired["trust_assessment"]["band"], "guarded")
        self.assertTrue(impaired["trust_assessment"]["weak_soft_decisions"])
        self.assertTrue(impaired["acquisition"]["acquisition_success"])
        self.assertEqual(
            impaired["acquisition"]["detected_timing_offset"],
            self.impaired_metadata["true_timing_offset"],
        )
        self.assertEqual(
            impaired["acquisition"]["detected_cfo_hz"],
            self.impaired_metadata["true_cfo_hz"],
        )
        self.assertLess(
            impaired["acquisition"]["confidence"],
            healthy["acquisition"]["confidence"],
        )
        self.assertLess(impaired["trust_score"], healthy["trust_score"])
        self.assertLess(
            impaired["trust_features"]["mean_abs_soft_decision"],
            healthy["trust_features"]["mean_abs_soft_decision"],
        )
        self.assertGreater(
            impaired["trust_features"]["weak_soft_decision_fraction"],
            healthy["trust_features"]["weak_soft_decision_fraction"],
        )

    def test_ambiguous_replay_decodes_with_competing_acquisition_peak(self) -> None:
        healthy = run_json_command("bash", "scripts/run_replay_demo.sh")
        result = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq",
        )
        assert_subset(self, result, load_golden("replay_ambiguous.json"))
        assert_decoder_reporting(self, result)

        self.assertTrue(result["ok"])
        self.assertTrue(result["acquisition"]["acquisition_success"])
        self.assertEqual(result["decoded_text"], self.ambiguous_metadata["message"])
        self.assertEqual(
            result["acquisition"]["detected_timing_offset"],
            self.ambiguous_metadata["true_timing_offset"],
        )
        self.assertEqual(
            result["acquisition"]["detected_cfo_hz"],
            self.ambiguous_metadata["true_cfo_hz"],
        )
        self.assertTrue(result["trust_assessment"]["ambiguous_acquisition"])
        self.assertEqual(result["trust_assessment"]["band"], "guarded")
        self.assertLess(
            result["acquisition"]["normalized_peak_separation"], 0.2
        )
        self.assertLess(result["trust_score"], healthy["trust_score"])

    def test_failed_replay_is_crc_rejected_and_low_confidence(self) -> None:
        result = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "--allow-failure",
            "data/synthetic/canned_replay/demo_conv_bpsk_failed.iq",
        )
        impaired = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq",
        )
        assert_subset(self, result, load_golden("replay_failed.json"))
        assert_decoder_reporting(self, result)

        self.assertFalse(result["ok"])
        self.assertFalse(result["crc_ok"])
        self.assertEqual(result["samples_per_symbol"], self.failed_metadata["samples_per_symbol"])
        self.assertEqual(result["trust_assessment"]["band"], "low-confidence")
        self.assertTrue(result["trust_assessment"]["crc_failed"])
        self.assertFalse(result["trust_assessment"]["crc_not_evaluated"])
        self.assertTrue(result["acquisition"]["acquisition_success"])
        self.assertEqual(
            result["acquisition"]["ground_truth"]["timing_error_samples"], 0
        )
        self.assertEqual(
            result["acquisition"]["ground_truth"]["cfo_hypothesis_error_hz"], 0
        )
        self.assertLess(result["trust_score"], impaired["trust_score"])
        self.assertEqual(result["error"], "CRC mismatch")

    def test_noise_only_replay_is_rejected_before_demodulation(self) -> None:
        result = run_json_command(
            "bash",
            "scripts/run_replay_demo.sh",
            "--allow-failure",
            "data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq",
        )
        assert_subset(self, result, load_golden("replay_no_signal.json"))
        assert_decoder_reporting(self, result)

        self.assertFalse(result["ok"])
        self.assertFalse(result["acquisition"]["acquisition_success"])
        self.assertFalse(
            result["acquisition"]["ground_truth"]["signal_present"]
        )
        self.assertLess(
            result["acquisition"]["normalized_peak"],
            result["acquisition"]["minimum_normalized_peak"],
        )
        self.assertEqual(result["demod"]["symbol_count"], 0)
        self.assertEqual(result["frame_soft_bits"], 0)
        self.assertTrue(result["trust_assessment"]["acquisition_rejected"])
        self.assertTrue(result["trust_assessment"]["crc_not_evaluated"])
        self.assertFalse(result["trust_assessment"]["crc_failed"])
        self.assertEqual(result["trust_assessment"]["band"], "low-confidence")

    def test_trust_comparison_script_has_expected_progression(self) -> None:
        result = run_json_command("bash", "scripts/compare_trust_cases.sh")
        assert_subset(self, result, load_golden("trust_case_comparison.json"))

        self.assertEqual(
            result["comparison"]["trust_band_progression"],
            [
                "high-confidence",
                "guarded",
                "guarded",
                "low-confidence",
                "low-confidence",
            ],
        )
        self.assertTrue(result["comparison"]["trust_score_order_ok"])
        self.assertTrue(result["comparison"]["acquisition_confidence_order_ok"])
        self.assertTrue(result["comparison"]["ambiguous_peak_detected"])
        self.assertTrue(result["comparison"]["no_signal_rejected_before_demod"])
        self.assertGreater(
            result["comparison"]["failed_score_delta"],
            result["comparison"]["impaired_score_delta"],
        )

    def test_metadata_fixture_is_in_sync_with_generator_contract(self) -> None:
        self.assertEqual(self.metadata["message"], "SATCOM DEMO OK")
        self.assertEqual(self.metadata["message_bytes"], 14)
        self.assertEqual(self.metadata["payload_bytes_with_crc"], 15)
        self.assertEqual(
            self.metadata["schema"], "satcom-fec-trust-lab/replay-fixture-v2"
        )
        self.assertEqual(self.metadata["sample_count"], 4096)
        self.assertEqual(self.metadata["preamble_length"], 256)
        self.assertEqual(self.metadata["true_timing_offset"], 192)
        self.assertEqual(self.metadata["true_cfo_hz"], 250.0)
        self.assertEqual(self.metadata["samples_per_symbol"], 8)
        self.assertEqual(self.metadata["coded_bits_per_frame"], 244)
        self.assertEqual(len(self.metadata["sync_word_bits"]), 16)
        self.assertEqual(self.impaired_metadata["scenario"], "impaired")
        self.assertEqual(self.impaired_metadata["message"], "SATCOM DEMO OK")
        self.assertEqual(self.impaired_metadata["samples_per_symbol"], 8)
        self.assertEqual(self.ambiguous_metadata["scenario"], "ambiguous")
        self.assertIsNotNone(self.ambiguous_metadata["distractor"])
        self.assertEqual(self.failed_metadata["scenario"], "failed")
        self.assertEqual(self.failed_metadata["message"], "SATCOM DEMO OK")
        self.assertEqual(self.failed_metadata["samples_per_symbol"], 8)
        self.assertEqual(self.failed_metadata["corruption_mode"], "invert")
        self.assertEqual(self.no_signal_metadata["scenario"], "no_signal")
        self.assertFalse(self.no_signal_metadata["signal_present"])
        self.assertFalse(self.no_signal_metadata["acquisition_expected"])

    def test_replay_fixture_generation_is_byte_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output_dir = Path(temporary_directory)
            preamble = output_dir / "preamble_qpsk_256.iq"
            for profile in ("healthy", "impaired", "ambiguous", "failed", "no_signal"):
                stem = "demo_conv_bpsk" if profile == "healthy" else f"demo_conv_bpsk_{profile}"
                generated_iq = output_dir / f"{stem}.iq"
                generated_metadata = output_dir / f"{stem}.json"
                subprocess.run(
                    [
                        "python3",
                        "scripts/generate_synthetic_iq.py",
                        "--profile",
                        profile,
                        "--output",
                        str(generated_iq),
                        "--metadata",
                        str(generated_metadata),
                        "--preamble-output",
                        str(preamble),
                    ],
                    cwd=ROOT_DIR,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                checked_in_iq = ROOT_DIR / "data/synthetic/canned_replay" / f"{stem}.iq"
                self.assertEqual(generated_iq.read_bytes(), checked_in_iq.read_bytes())
                generated = json.loads(generated_metadata.read_text(encoding="utf-8"))
                checked_in = json.loads(
                    (ROOT_DIR / "data/synthetic/canned_replay" / f"{stem}.json").read_text(
                        encoding="utf-8"
                    )
                )
                self.assertEqual(generated, checked_in)


if __name__ == "__main__":
    unittest.main()
