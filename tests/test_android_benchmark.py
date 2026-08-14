import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
BUILD_SCRIPT = ROOT_DIR / "scripts/build_android_benchmark.sh"
RUN_SCRIPT = ROOT_DIR / "scripts/run_android_benchmark.sh"
VERIFY_SCRIPT = ROOT_DIR / "scripts/verify_android_benchmark_build.sh"


class AndroidBenchmarkBuildContractTests(unittest.TestCase):
    def test_scripts_have_valid_shell_syntax_and_help(self) -> None:
        for script in (BUILD_SCRIPT, RUN_SCRIPT, VERIFY_SCRIPT):
            with self.subTest(script=script.name):
                subprocess.run(
                    ("bash", "-n", str(script)),
                    cwd=ROOT_DIR,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                completed = subprocess.run(
                    ("bash", str(script), "--help"),
                    cwd=ROOT_DIR,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                self.assertIn("Usage:", completed.stdout)

    def test_scripts_are_checked_in_as_executable(self) -> None:
        for script in (BUILD_SCRIPT, RUN_SCRIPT, VERIFY_SCRIPT):
            with self.subTest(script=script.name):
                mode = os.stat(script).st_mode
                self.assertTrue(mode & stat.S_IXUSR)

    def test_android_only_mode_rejects_a_host_toolchain(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            completed = subprocess.run(
                (
                    "cmake",
                    "-S",
                    str(ROOT_DIR),
                    "-B",
                    temporary_directory,
                    "-DSATCOMFEC_ANDROID_BENCHMARK_ONLY=ON",
                    "-DBUILD_TESTING=OFF",
                ),
                cwd=ROOT_DIR,
                check=False,
                capture_output=True,
                text=True,
            )
        self.assertNotEqual(completed.returncode, 0)
        output = completed.stdout + completed.stderr
        self.assertIn("SATCOMFEC_ANDROID_BENCHMARK_ONLY=ON requires", output)
        self.assertIn("Android NDK CMake", output)


if __name__ == "__main__":
    unittest.main()
