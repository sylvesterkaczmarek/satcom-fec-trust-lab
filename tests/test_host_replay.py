import json
import subprocess
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]


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
    def test_canned_replay_decodes_expected_payload(self) -> None:
        result = run_json_command("bash", "scripts/run_replay_demo.sh")

        self.assertTrue(result["ok"])
        self.assertEqual(result["decoded_text"], "SATCOM DEMO OK")
        self.assertTrue(result["crc_ok"])
        self.assertGreater(result["sync_score"], 0)
        self.assertGreater(result["mean_abs_llr"], 0.0)
        self.assertGreaterEqual(result["trust_score"], 0.0)
        self.assertLessEqual(result["trust_score"], 1.0)

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
        self.assertEqual(result["decoded_text"], "SATCOM DEMO OK")
        self.assertTrue(result["assumptions"]["same_input_frame"])
        self.assertTrue(result["assumptions"]["same_decoder_settings"])
        self.assertTrue(result["assumptions"]["same_evaluation_window"])

        paths = {path["decoder"]: path for path in result["paths"]}
        self.assertEqual(paths["viterbi-neon"]["implementation_class"], "real")
        self.assertEqual(paths["viterbi-sme2"]["implementation_class"], "simplified")


if __name__ == "__main__":
    unittest.main()
