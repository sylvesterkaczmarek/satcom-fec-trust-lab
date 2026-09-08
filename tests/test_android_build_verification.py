import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]


class AndroidBuildVerificationTests(unittest.TestCase):
    """Check verifier wiring using synthetic build artefacts and LLVM tools."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="satcom-android-verify-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.scripts = self.root / "scripts"
        self.scripts.mkdir()
        for name in (
            "verify_android_benchmark_build.sh",
            "check_compile_commands.py",
            "check_neon_disassembly.sh",
        ):
            shutil.copyfile(ROOT_DIR / "scripts" / name, self.scripts / name)
        (self.scripts / "build_android_benchmark.sh").write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'printf "%s\\n" "$@" > "${SATCOM_TEST_BUILD_ARGUMENTS}"\n',
            encoding="utf-8",
        )
        self.default_output = self.root / "build/android/arm64-v8a"
        self.inspection_log = self.root / "inspection.log"
        self.arguments_log = self.root / "build-arguments.log"

    def make_ndk(self, name: str) -> Path:
        ndk = self.root / name
        tools = ndk / "toolchains/llvm/prebuilt/test-host/bin"
        tools.mkdir(parents=True)
        bodies = {
            "llvm-readelf": '''case "$1" in
  -h) printf 'Machine: AArch64\nType: DYN\n' ;;
  -d) printf 'NEEDED libc.so\n' ;;
  *) exit 2 ;;
esac
''',
            "llvm-nm": "printf 'acquisition_neon_kernel_compiled\\n"
            "acquisition_sme2_kernel_compiled\\n'\n",
            "llvm-objdump": '''case "$2" in
  */acquisition_neon.cpp.o)
    printf 'ld2 {v0.4s, v1.4s}, [x0]\nfmla v2.4s, v0.4s, v1.4s\n' ;;
  *) printf 'ret\n' ;;
esac
''',
        }
        for name, body in bodies.items():
            path = tools / name
            path.write_text(
                '#!/usr/bin/env bash\nset -euo pipefail\n'
                'printf "%s\\n" "$0" "$@" >> "${SATCOM_TEST_INSPECTION_LOG}"\n'
                + body,
                encoding="utf-8",
            )
            path.chmod(0o755)
        return ndk

    def make_build(self, output: Path, ndk: Path) -> None:
        build = output / "baseline"
        objects = build / "CMakeFiles/satcom_replay_core.dir/src/acquisition"
        objects.mkdir(parents=True)
        source_flags = {
            "acquisition_reference": ["-fno-vectorize", "-fno-slp-vectorize"],
            "acquisition_neon": ["-DSATCOMFEC_ACQUISITION_NEON_COMPILED=1"],
            "acquisition_sme2": [],
            "acquisition_sme2_kernel": [],
        }
        entries = []
        for stem, flags in source_flags.items():
            (objects / f"{stem}.cpp.o").write_bytes(b"synthetic object")
            entries.append(
                {
                    "directory": str(self.root),
                    "file": f"src/acquisition/{stem}.cpp",
                    "arguments": ["synthetic-clang++", *flags],
                }
            )
        (build / "compile_commands.json").write_text(
            json.dumps(entries), encoding="utf-8"
        )
        (output / "benchmark_acquisition").write_bytes(b"synthetic ELF")
        (output / "last-build.env").write_text(
            f"ANDROID_NDK_HOME={ndk}\n"
            "SATCOMFEC_ANDROID_SME2_COMPILED=OFF\n"
            f"ANDROID_BENCHMARK_BINARY={output / 'benchmark_acquisition'}\n"
            f"ANDROID_BENCHMARK_BUILD_DIR={build}\n",
            encoding="utf-8",
        )

    def verify(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            ["bash", str(self.scripts / "verify_android_benchmark_build.sh"), *arguments],
            cwd=self.root,
            env={
                **os.environ,
                "SATCOMFEC_ANDROID_ABI": "arm64-v8a",
                "SATCOM_TEST_INSPECTION_LOG": str(self.inspection_log),
                "SATCOM_TEST_BUILD_ARGUMENTS": str(self.arguments_log),
            },
            capture_output=True,
            text=True,
            check=False,
            timeout=15,
        )

    def test_custom_output_uses_selected_build_despite_stale_default(self) -> None:
        ndk = self.make_ndk("ndk")
        selected = self.root / "selected output"
        self.make_build(self.default_output, ndk)
        self.make_build(selected, ndk)
        stale_metadata = (self.default_output / "last-build.env").read_bytes()
        arguments = ("--sme2", "off", "--output-dir", str(selected))

        result = self.verify(*arguments)

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Android benchmark build verification passed", result.stdout)
        self.assertEqual(self.arguments_log.read_text().splitlines(), list(arguments))
        self.assertIn(str(selected / "benchmark_acquisition"), self.inspection_log.read_text())
        self.assertNotIn(str(self.default_output), self.inspection_log.read_text())
        self.assertTrue((selected / "acquisition_neon.disassembly.txt").is_file())
        self.assertFalse((self.default_output / "acquisition_neon.disassembly.txt").exists())
        self.assertEqual((self.default_output / "last-build.env").read_bytes(), stale_metadata)

    def test_ndk_tools_run_when_installation_path_contains_spaces(self) -> None:
        ndk = self.make_ndk("Android NDK with spaces")
        self.make_build(self.default_output, ndk)

        result = self.verify("--ndk", str(ndk), "--sme2", "off")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        inspection = self.inspection_log.read_text()
        for name in ("llvm-readelf", "llvm-nm", "llvm-objdump"):
            self.assertIn(str(ndk / "toolchains/llvm/prebuilt/test-host/bin" / name), inspection)

    def test_missing_custom_metadata_does_not_use_stale_default(self) -> None:
        ndk = self.make_ndk("ndk")
        self.make_build(self.default_output, ndk)
        selected = self.root / "missing-build"

        result = self.verify("--output-dir", str(selected))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(selected / "last-build.env"), result.stderr)
        self.assertFalse(self.inspection_log.exists())


if __name__ == "__main__":
    unittest.main()
