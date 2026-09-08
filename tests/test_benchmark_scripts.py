"""Exercise report handling with synthetic subprocesses and no connected device."""

import copy
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT_DIR = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "repeat_acquisition_benchmark", ROOT_DIR / "scripts/repeat_acquisition_benchmark.py"
)
REPEAT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPEAT)


def sample_report():
    return json.loads(
        (ROOT_DIR / "benchmarks/results/b6ed1ec/run-01.json").read_text(encoding="utf-8")
    )


class RepeatReportSafetyTests(unittest.TestCase):
    def test_archived_report_summaries_remain_identical(self):
        for archive in ("a83cd53", "b6ed1ec"):
            with self.subTest(archive=archive):
                directory = ROOT_DIR / "benchmarks/results" / archive
                reports = [
                    json.loads(path.read_text(encoding="utf-8"))
                    for path in sorted(directory.glob("run-*.json"))
                ]
                expected = json.loads((directory / "summary.json").read_text(encoding="utf-8"))
                actual = REPEAT.summarize_reports(reports)
                for key in actual:
                    if key != "generated_utc":
                        self.assertEqual(actual[key], expected[key], key)

    def test_existing_results_are_preserved_before_build_or_run(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "run-01.json").write_text("previous raw report", encoding="utf-8")
            (output / "summary.json").write_text("previous summary", encoding="utf-8")
            with mock.patch.object(sys, "argv", ["repeat", "--output-dir", directory]):
                with mock.patch.object(REPEAT.subprocess, "run") as run:
                    with self.assertRaisesRegex(RuntimeError, "new or empty"):
                        REPEAT.main()
                    run.assert_not_called()
            self.assertEqual((output / "run-01.json").read_text(), "previous raw report")
            self.assertEqual((output / "summary.json").read_text(), "previous summary")

    def test_nonfinite_sample_duration_is_rejected_before_execution(self):
        for value in ("nan", "inf", "-inf"):
            with self.subTest(value=value):
                with mock.patch.object(sys, "argv", ["repeat", f"--min-sample-ms={value}"]):
                    with mock.patch.object(sys, "stderr"):
                        with self.assertRaises(SystemExit) as raised:
                            REPEAT.parse_arguments()
                self.assertEqual(raised.exception.code, 2)

    def test_changed_benchmark_settings_cannot_be_combined(self):
        for setting in ("warmup_rounds", "timed_sample_count", "deterministic_seed"):
            with self.subTest(setting=setting):
                first = sample_report()
                second = copy.deepcopy(first)
                second["benchmark"][setting] += 1
                with self.assertRaisesRegex(RuntimeError, "benchmark settings changed"):
                    REPEAT.summarize_reports([first, second])

    def test_duplicate_observations_are_rejected(self):
        for kind in ("workload", "implementation", "mode"):
            with self.subTest(kind=kind):
                report = sample_report()
                workloads = report["workloads"]
                implementations = workloads[0]["implementations"]
                modes = implementations[0]["modes"]
                target = {"workload": workloads, "implementation": implementations, "mode": modes}[kind]
                target.append(copy.deepcopy(target[0]))
                with self.assertRaisesRegex(RuntimeError, "duplicate"):
                    REPEAT.summarize_reports([report])

    def test_invalid_success_and_median_values_are_rejected(self):
        for value in (False, "true", 1, None):
            with self.subTest(ok=value):
                report = sample_report()
                report["ok"] = value
                with self.assertRaisesRegex(RuntimeError, "ok=true"):
                    REPEAT.summarize_reports([report])
        for value in (float("nan"), float("inf"), -1, 0, True, "1.0"):
            with self.subTest(median=value):
                report = sample_report()
                report["workloads"][0]["implementations"][0]["modes"][0]["timing"]["latency_ms"]["median"] = value
                with self.assertRaisesRegex(RuntimeError, "finite positive median"):
                    REPEAT.summarize_reports([report])

    def test_failed_new_run_preserves_raw_output_without_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            binary = build / "benchmark_acquisition"
            binary.touch(mode=0o700)
            output = root / "output"
            command = ["repeat", "--skip-build", "--build-dir", str(build), "--output-dir", str(output)]
            failed = subprocess.CompletedProcess([], 1, "partial report", "failure detail")
            with mock.patch.object(sys, "argv", command):
                with mock.patch.object(REPEAT.subprocess, "run", return_value=failed):
                    with self.assertRaisesRegex(RuntimeError, "failed with exit code 1"):
                        REPEAT.main()
            self.assertEqual((output / "run-01.json").read_text(), "partial report")
            self.assertEqual((output / "run-01.stderr.txt").read_text(), "failure detail")
            self.assertFalse((output / "summary.json").exists())


FAKE_ADB = r'''
import json
import os
import shlex
import shutil
import sys
import uuid
from pathlib import Path

root = Path(os.environ["FAKE_ADB_ROOT"])
args = sys.argv[1:]
with (root / "calls.jsonl").open("a", encoding="utf-8") as log:
    log.write(json.dumps(args) + "\n")
if args[:1] == ["-s"]:
    args = args[2:]

def mapped(remote):
    return root / "remote" / remote.lstrip("/")

if args == ["get-state"]:
    print("device")
elif args[:1] == ["shell"]:
    words = shlex.split(" ".join(args[1:]))
    if words[:2] == ["getprop", "ro.product.cpu.abi"]:
        print("arm64-v8a")
    elif words[:2] == ["mktemp", "-d"]:
        remote = words[2].replace("XXXXXX", uuid.uuid4().hex)
        mapped(remote).mkdir(parents=True)
        print(remote)
    elif words[:2] == ["mkdir", "-p"]:
        mapped(words[2]).mkdir(parents=True, exist_ok=True)
    elif words[:2] == ["rm", "-rf"]:
        shutil.rmtree(mapped(words[2]))
    elif words[:1] == ["chmod"]:
        pass
    else:
        (root / "benchmark-arguments.json").write_text(json.dumps(words[1:]))
        if "--help" not in words:
            destination = words[words.index("--json") + 1]
            mapped(destination).write_text(os.environ["FAKE_ADB_REPORT"], encoding="utf-8")
elif args[:1] == ["push"]:
    shutil.copyfile(args[1], mapped(args[2]))
elif args[:1] == ["pull"]:
    if not mapped(args[1]).is_file():
        sys.exit(1)
    shutil.copyfile(mapped(args[1]), args[2])
    if os.environ.get("FAKE_ADB_RACE_OUTPUT"):
        Path(os.environ["FAKE_ADB_RACE_OUTPUT"]).write_text("other completed result")
else:
    sys.exit(2)
'''


class AndroidReportSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        scripts = self.root / "scripts"
        scripts.mkdir()
        self.script = scripts / "run_android_benchmark.sh"
        shutil.copyfile(ROOT_DIR / "scripts/run_android_benchmark.sh", self.script)
        binary = self.root / "build/android/arm64-v8a/benchmark_acquisition"
        binary.parent.mkdir(parents=True)
        binary.write_text("synthetic binary")
        tools = self.root / "fake tools"
        tools.mkdir()
        adb = tools / "adb"
        adb.write_text(f"#!{sys.executable}\n" + FAKE_ADB, encoding="utf-8")
        adb.chmod(0o700)
        self.environment = os.environ.copy()
        self.environment.update({
            "PATH": str(tools) + os.pathsep + self.environment["PATH"],
            "FAKE_ADB_ROOT": str(self.root),
            "FAKE_ADB_REPORT": json.dumps(sample_report()),
            "SATCOMFEC_ANDROID_ABI": "arm64-v8a",
        })
        self.output = self.root / "results with spaces/report.json"

    def run_script(self, *arguments):
        return subprocess.run(
            ["bash", str(self.script), "--skip-build", "--output", str(self.output), *arguments],
            env=self.environment, capture_output=True, text=True, check=False,
        )

    def test_success_preserves_report_bytes_and_default_workload(self):
        completed = self.run_script()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(self.output.read_text(), self.environment["FAKE_ADB_REPORT"])
        arguments = json.loads((self.root / "benchmark-arguments.json").read_text())
        self.assertEqual(arguments[:-2], ["--workload", "small", "--warmup-rounds", "1", "--samples", "7", "--min-sample-ms", "20"])
        self.assertFalse(list(self.output.parent.glob("*.tmp.*")))
        self.assertFalse(list((self.root / "remote/data/local/tmp").glob("satcom-fec-trust-lab.*")))

    def test_argument_boundaries_survive_remote_shell(self):
        value = "value with spaces; '$literal' $(unused)"
        completed = self.run_script("--", "--workload", value)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        arguments = json.loads((self.root / "benchmark-arguments.json").read_text())
        self.assertEqual(arguments[:2], ["--workload", value])

    def test_help_cannot_reuse_a_previous_remote_result(self):
        stale = self.root / "remote/data/local/tmp/satcom-fec-trust-lab/acquisition-result.json"
        stale.parent.mkdir(parents=True)
        stale.write_text(self.environment["FAKE_ADB_REPORT"])
        completed = self.run_script("--", "--help")
        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(self.output.exists())
        self.assertEqual(stale.read_text(), self.environment["FAKE_ADB_REPORT"])

    def test_invalid_report_is_never_published(self):
        nonfinite = sample_report()
        nonfinite["workloads"][0]["implementations"][0]["modes"][0]["timing"]["latency_ms"]["median"] = float("nan")
        for contents in ("not JSON", "[]", '{"ok": true}', '{"ok": false}', json.dumps(nonfinite)):
            with self.subTest(contents=contents):
                self.environment["FAKE_ADB_REPORT"] = contents
                completed = self.run_script()
                self.assertNotEqual(completed.returncode, 0)
                self.assertFalse(self.output.exists())
                self.assertFalse(list(self.output.parent.glob("*.tmp.*")))

    def test_existing_output_is_preserved_before_adb_use(self):
        self.output.parent.mkdir()
        self.output.write_text("accepted result")
        completed = self.run_script()
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("output already exists", completed.stderr)
        self.assertEqual(self.output.read_text(), "accepted result")
        self.assertFalse((self.root / "calls.jsonl").exists())

    def test_concurrent_output_is_preserved_when_report_is_published(self):
        self.environment["FAKE_ADB_RACE_OUTPUT"] = str(self.output)
        completed = self.run_script()
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(self.output.read_text(), "other completed result")
        self.assertFalse(list(self.output.parent.glob("*.tmp.*")))


if __name__ == "__main__":
    unittest.main()
