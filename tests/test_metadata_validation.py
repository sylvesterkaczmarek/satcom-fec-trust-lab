import ctypes
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
BUILD_DIR = Path(os.environ.get("SATCOMFEC_TEST_BUILD_DIR", "build/host_replay"))
if not BUILD_DIR.is_absolute():
    BUILD_DIR = ROOT_DIR / BUILD_DIR
ACQUISITION_DIR = ROOT_DIR / "data/synthetic/acquisition"
REPLAY_DIR = ROOT_DIR / "data/synthetic/canned_replay"


class MetadataValidationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        environment = os.environ.copy()
        environment["SATCOMFEC_BUILD_DIR"] = str(BUILD_DIR)
        for target in ("acquisition_demo", "replay_demo"):
            subprocess.run(
                ["bash", "scripts/build_host_tools.sh", target],
                cwd=ROOT_DIR, env=environment, check=True, capture_output=True, text=True,
            )

    def run_metadata(self, target: str, metadata: dict, *, raw_field=None):
        fixture_dir = ACQUISITION_DIR if target == "acquisition_demo" else REPLAY_DIR
        fixture_name = "clean" if target == "acquisition_demo" else "demo_conv_bpsk"
        metadata = dict(metadata)
        metadata["preamble_file"] = str(fixture_dir / "preamble_qpsk_256.iq")
        document = json.dumps(metadata)
        if raw_field is not None:
            key, token = raw_field
            original = json.dumps(key) + ": " + json.dumps(metadata[key])
            document = document.replace(original, json.dumps(key) + ": " + token, 1)
        with tempfile.TemporaryDirectory() as temporary_directory:
            metadata_path = Path(temporary_directory) / "fixture.json"
            metadata_path.write_text(document, encoding="utf-8")
            return subprocess.run(
                [str(BUILD_DIR / target), "--iq", str(fixture_dir / f"{fixture_name}.iq"),
                 "--metadata", str(metadata_path)],
                cwd=ROOT_DIR, capture_output=True, text=True, timeout=15,
            )

    def test_acquisition_rejects_invalid_numeric_tokens(self) -> None:
        metadata = json.loads((ACQUISITION_DIR / "clean.json").read_text())
        for field, token in (
            ("sample_count", "4096garbage"),
            ("sample_count", "+4096"),
            ("sample_count", "04096"),
            ("sample_count", "4096.5"),
            ("sample_count", "18446744073709551616"),
            ("sample_count", "18446744073709551616.0"),
            ("sample_count", "1e999999999999999999999"),
            ("sample_rate_hz", "100000junk"),
            ("sample_rate_hz", "0x1p4"),
            ("sample_rate_hz", "1e9999"),
        ):
            with self.subTest(field=field, token=token):
                completed = self.run_metadata("acquisition_demo", metadata, raw_field=(field, token))
                self.assertEqual(completed.returncode, 1)
                report = json.loads(completed.stdout)
                self.assertFalse(report["ok"])
                self.assertIn("metadata", report["error"])
                self.assertNotIn("IQ sample count", report["error"])

    def test_acquisition_rejects_malformed_number_arrays(self) -> None:
        metadata = json.loads((ACQUISITION_DIR / "clean.json").read_text())
        for token in ("[0 500]", "[0,,500]", "[0,500,]", "[0junk,500]", "[]"):
            with self.subTest(token=token):
                completed = self.run_metadata(
                    "acquisition_demo", metadata, raw_field=("cfo_hypotheses_hz", token)
                )
                self.assertEqual(completed.returncode, 1)
                self.assertFalse(json.loads(completed.stdout)["ok"])

    def test_size_limit_is_checked_without_floating_point_rounding(self) -> None:
        metadata = json.loads((ACQUISITION_DIR / "clean.json").read_text())
        maximum = (1 << (8 * ctypes.sizeof(ctypes.c_size_t))) - 1
        for token in (str(maximum), f"{maximum}.0", f"{maximum * 100}e-2"):
            with self.subTest(token=token):
                completed = self.run_metadata(
                    "acquisition_demo", metadata, raw_field=("sample_count", token)
                )
                self.assertEqual(completed.returncode, 1)
                self.assertEqual(json.loads(completed.stdout)["error"],
                                 "IQ sample count does not match metadata")
        for token in (str(maximum + 1), f"{maximum + 1}.0", f"{(maximum + 1) * 100}e-2"):
            with self.subTest(token=token):
                completed = self.run_metadata(
                    "acquisition_demo", metadata, raw_field=("sample_count", token)
                )
                self.assertEqual(completed.returncode, 1)
                self.assertIn("missing a required", json.loads(completed.stdout)["error"])

    def test_acquisition_range_is_rejected_before_expanding_hypotheses(self) -> None:
        original = json.loads((ACQUISITION_DIR / "clean.json").read_text())
        for updates in (
            {"timing_search_stop_inclusive": 10**12},
            {"preamble_length": 4097},
            {"timing_search_stop_inclusive": 3839, "timing_search_step": 3},
        ):
            with self.subTest(updates=updates):
                completed = self.run_metadata("acquisition_demo", original | updates)
                self.assertEqual(completed.returncode, 1)
                self.assertIn("range", json.loads(completed.stdout)["error"])

    def test_integer_valued_json_forms_preserve_acquisition_results(self) -> None:
        metadata = json.loads((ACQUISITION_DIR / "clean.json").read_text())
        expected = json.loads(self.run_metadata("acquisition_demo", metadata).stdout)
        expected.pop("metadata_path")
        # This tool also supports oracle metadata that omits the schema field.
        metadata.pop("schema")
        for token in ("4096.0", "4.096e3", "409600e-2", "409600000000000000000000e-20"):
            with self.subTest(token=token):
                completed = self.run_metadata(
                    "acquisition_demo", metadata, raw_field=("sample_count", token)
                )
                self.assertEqual(completed.returncode, 0, completed.stdout)
                actual = json.loads(completed.stdout)
                actual.pop("metadata_path")
                self.assertEqual(actual, expected)

    def test_replay_rejects_invalid_numeric_and_literal_tokens(self) -> None:
        metadata = json.loads((REPLAY_DIR / "demo_conv_bpsk.json").read_text())
        for field, token in (
            ("samples_per_symbol", "8.5"),
            ("samples_per_symbol", "8junk"),
            ("samples_per_symbol", "18446744073709551616"),
            ("samples_per_symbol", "0"),
            ("sample_rate_hz", "48000junk"),
            ("sample_rate_hz", "0"),
            ("true_timing_offset", "192.5"),
            ("true_cfo_hz", "250e"),
            ("true_cfo_hz", "nulljunk"),
            ("signal_present", "truejunk"),
        ):
            with self.subTest(field=field, token=token):
                completed = self.run_metadata("replay_demo", metadata, raw_field=(field, token))
                self.assertEqual(completed.returncode, 1)
                self.assertEqual(completed.stdout, "")
                self.assertIn("metadata", completed.stderr)

    def test_integer_valued_json_forms_preserve_replay_results(self) -> None:
        metadata = json.loads((REPLAY_DIR / "demo_conv_bpsk.json").read_text())
        expected = json.loads(self.run_metadata("replay_demo", metadata).stdout)
        expected.pop("metadata_path")
        for token in ("8.0", "8e0", "80e-1"):
            with self.subTest(token=token):
                completed = self.run_metadata(
                    "replay_demo", metadata, raw_field=("samples_per_symbol", token)
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                actual = json.loads(completed.stdout)
                actual.pop("metadata_path")
                self.assertEqual(actual, expected)

    def test_failed_inferred_metadata_preserves_all_default_configuration(self) -> None:
        metadata = json.loads((REPLAY_DIR / "demo_conv_bpsk.json").read_text())
        metadata.update(sample_rate_hz=24000, samples_per_symbol=4)
        metadata.pop("preamble_file")
        with tempfile.TemporaryDirectory() as temporary_directory:
            iq_path = Path(temporary_directory) / "capture.iq"
            shutil.copyfile(REPLAY_DIR / "demo_conv_bpsk.iq", iq_path)
            command = [str(BUILD_DIR / "replay_demo"), "--iq", str(iq_path),
                       "--preamble", str(REPLAY_DIR / "preamble_qpsk_256.iq")]
            before = subprocess.run(command, cwd=ROOT_DIR, capture_output=True, text=True, check=True)
            iq_path.with_suffix(".json").write_text(json.dumps(metadata), encoding="utf-8")
            after = subprocess.run(command, cwd=ROOT_DIR, capture_output=True, text=True, check=True)
        self.assertEqual(after.stdout, before.stdout)
        self.assertEqual(json.loads(after.stdout)["metadata_path"], "")


if __name__ == "__main__":
    unittest.main()
