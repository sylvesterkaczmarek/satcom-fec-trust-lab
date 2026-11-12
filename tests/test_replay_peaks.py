import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
FIXTURES = ROOT_DIR / "data/synthetic/canned_replay"
BUILD_DIR = ROOT_DIR / os.environ.get("SATCOMFEC_TEST_BUILD_DIR", "build/host_replay")


class ReplayPeakTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(
            ["bash", "scripts/build_host_tools.sh", "replay_demo"],
            cwd=ROOT_DIR, env={**os.environ, "SATCOMFEC_BUILD_DIR": str(BUILD_DIR)},
            check=True, capture_output=True,
        )

    def replay(self, fixture, frequencies, *, single_timing=False, implementation="reference"):
        metadata = json.loads((FIXTURES / f"{fixture}.json").read_text(encoding="utf-8"))
        metadata["cfo_hypotheses_hz"] = list(frequencies)
        iq_path = FIXTURES / f"{fixture}.iq"
        with tempfile.TemporaryDirectory() as directory:
            if single_timing:
                start = metadata["true_timing_offset"] * 8  # two float32 components
                size = (metadata["preamble_length"] + metadata["frame_sample_count"]) * 8
                capture = iq_path.read_bytes()[start:start + size]
                iq_path = Path(directory) / "cropped.iq"
                iq_path.write_bytes(capture)
                metadata["true_timing_offset"] = 0
                metadata["sample_count"] = len(capture) // 8
                metadata["timing_search_stop_inclusive"] = 0
            metadata_path = Path(directory) / "fixture.json"
            metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
            process = subprocess.run(
                [str(BUILD_DIR / "replay_demo"), "--iq", str(iq_path),
                 "--metadata", str(metadata_path), "--preamble",
                 str(FIXTURES / metadata["preamble_file"]), "--acquisition", implementation],
                cwd=ROOT_DIR, capture_output=True, text=True,
            )
        return json.loads(process.stdout)

    def test_dense_cfo_grid_preserves_healthy_decode_and_physical_distractor(self):
        for implementation in ("reference", "neon", "sme2"):
            for fixture, timing, cfo, band in (
                ("demo_conv_bpsk", 192, 250.0, "high-confidence"),
                ("demo_conv_bpsk_ambiguous", 1500, 0.0, "guarded"),
            ):
                with self.subTest(implementation=implementation, fixture=fixture):
                    report = self.replay(fixture, range(-500, 501, 10), implementation=implementation)
                    acquisition = report["acquisition"]
                    if implementation != "reference" and acquisition["selected_implementation"] == "unavailable":
                        break
                    self.assertTrue(report["crc_ok"])
                    self.assertEqual(report["trust_assessment"]["band"], band)
                    self.assertEqual(acquisition["selected_implementation"], implementation)
                    self.assertEqual(acquisition["detected_timing_offset"], timing)
                    self.assertEqual(acquisition["detected_cfo_hz"], cfo)
                    self.assertLess(acquisition["normalized_peak_separation"], 0.05)
                    self.assertGreater(acquisition["distinct_peak_separation"], 0.05)
                    if fixture == "demo_conv_bpsk_ambiguous":
                        competitor = acquisition["distinct_competitor"]
                        self.assertEqual(competitor["timing_offset"], 300)
                        self.assertEqual(competitor["cfo_hz"], -500.0)
                        self.assertTrue(report["trust_assessment"]["ambiguous_acquisition"])

    def test_same_timing_requires_a_competitor_outside_the_cfo_guard(self):
        fixture = "demo_conv_bpsk"
        unresolved = self.replay(fixture, [240.0, 250.0, 260.0], single_timing=True)
        acquisition = unresolved["acquisition"]
        self.assertEqual(acquisition["timing_hypothesis_count"], 1)
        self.assertEqual(acquisition["cfo_guard_hz"], 187.5)
        self.assertIsNone(acquisition["distinct_competitor"])
        self.assertIsNone(acquisition["distinct_peak_separation"])
        self.assertIsNone(unresolved["trust_features"]["acquisition_peak_separation"])
        self.assertIsNone(unresolved["trust_breakdown"]["acquisition_separation"])
        self.assertIsNone(unresolved["trust_breakdown"]["acquisition_certainty"])
        self.assertFalse(acquisition["acquisition_success"])
        self.assertTrue(unresolved["trust_assessment"]["crc_not_evaluated"])

        resolved = self.replay(fixture, [250.0, 437.5], single_timing=True)
        self.assertTrue(resolved["crc_ok"])
        competitor = resolved["acquisition"]["distinct_competitor"]
        self.assertEqual(competitor["timing_offset"], 0)
        self.assertEqual(competitor["cfo_hz"], 437.5)


if __name__ == "__main__":
    unittest.main()
