import json
import subprocess
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk.json"
IMPAIRED_METADATA_PATH = ROOT_DIR / "data/synthetic/canned_replay/demo_conv_bpsk_impaired.json"


def run_json_command(*args: str) -> dict:
    completed = subprocess.run(
        args,
        cwd=ROOT_DIR,
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


class HostReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.metadata = json.loads(METADATA_PATH.read_text(encoding="utf-8"))
        cls.impaired_metadata = json.loads(
            IMPAIRED_METADATA_PATH.read_text(encoding="utf-8")
        )

    def test_canned_replay_decodes_expected_payload(self) -> None:
        result = run_json_command("bash", "scripts/run_replay_demo.sh")

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
        self.assertGreater(result["trust_features"]["mean_abs_llr"], 0.0)
        self.assertGreaterEqual(result["trust_score"], 0.0)
        self.assertLessEqual(result["trust_score"], 1.0)
        self.assertEqual(result["trust_assessment"]["band"], "high-confidence")
        self.assertAlmostEqual(
            result["trust_score"],
            result["trust_breakdown"]["score"],
            places=6,
        )

    def test_neon_and_sme2_entrypoints_align_on_same_prepared_frame(self) -> None:
        result = run_json_command(
            "bash",
            "scripts/benchmark_decoder_paths.sh",
            "data/synthetic/canned_replay/demo_conv_bpsk.iq",
            "1",
            "5",
        )

        self.assertTrue(result["ok"])
        self.assertTrue(result["outputs_match"])
        self.assertTrue(result["benchmark"]["local_timing_only"])
        self.assertEqual(result["decoded_text"], self.metadata["message"])
        self.assertTrue(result["assumptions"]["same_input_frame"])
        self.assertTrue(result["assumptions"]["same_decoder_settings"])
        self.assertTrue(result["assumptions"]["same_evaluation_window"])
        self.assertTrue(result["assumptions"]["same_traceback_core"])
        self.assertTrue(result["assumptions"]["same_state_machine"])
        self.assertTrue(result["assumptions"]["same_prepared_soft_bits"])
        self.assertEqual(
            result["prepared_frame"]["frame_length"],
            self.metadata["coded_bits_per_frame"],
        )
        self.assertTrue(result["alignment"]["decoded_bit_count_match"])
        self.assertTrue(result["alignment"]["decoded_bit_checksum_match"])
        self.assertTrue(result["alignment"]["payload_text_match"])

        paths = {path["decoder"]: path for path in result["paths"]}
        self.assertEqual(paths["viterbi-neon"]["implementation_class"], "real")
        self.assertEqual(paths["viterbi-sme2"]["implementation_class"], "simplified")
        self.assertTrue(paths["viterbi-neon"]["decode_ok"])
        self.assertTrue(paths["viterbi-sme2"]["decode_ok"])
        self.assertEqual(
            paths["viterbi-neon"]["decoded_bit_count"],
            paths["viterbi-sme2"]["decoded_bit_count"],
        )
        self.assertEqual(
            paths["viterbi-neon"]["decoded_bit_checksum"],
            paths["viterbi-sme2"]["decoded_bit_checksum"],
        )

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

        self.assertTrue(impaired["ok"])
        self.assertEqual(impaired["decoded_text"], self.impaired_metadata["message"])
        self.assertEqual(
            impaired["samples_per_symbol"], self.impaired_metadata["samples_per_symbol"]
        )
        self.assertEqual(impaired["trust_assessment"]["band"], "guarded")
        self.assertTrue(impaired["trust_assessment"]["weak_soft_bits"])
        self.assertLess(impaired["trust_score"], healthy["trust_score"])
        self.assertLess(
            impaired["trust_features"]["mean_abs_llr"],
            healthy["trust_features"]["mean_abs_llr"],
        )
        self.assertGreater(
            impaired["trust_features"]["weak_llr_fraction"],
            healthy["trust_features"]["weak_llr_fraction"],
        )

    def test_metadata_fixture_is_in_sync_with_generator_contract(self) -> None:
        self.assertEqual(self.metadata["message"], "SATCOM DEMO OK")
        self.assertEqual(self.metadata["message_bytes"], 14)
        self.assertEqual(self.metadata["payload_bytes_with_crc"], 15)
        self.assertEqual(self.metadata["samples_per_symbol"], 8)
        self.assertEqual(self.metadata["coded_bits_per_frame"], 244)
        self.assertEqual(len(self.metadata["sync_word_bits"]), 16)
        self.assertEqual(self.impaired_metadata["scenario"], "impaired")
        self.assertEqual(self.impaired_metadata["message"], "SATCOM DEMO OK")
        self.assertEqual(self.impaired_metadata["samples_per_symbol"], 8)


if __name__ == "__main__":
    unittest.main()
