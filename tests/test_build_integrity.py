import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
FIXTURE_DIRECTORIES = (
    ROOT_DIR / "data/synthetic/acquisition",
    ROOT_DIR / "data/synthetic/canned_replay",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class FixtureIntegrityTests(unittest.TestCase):
    def test_tracked_fixture_manifest_matches_repository_bytes(self) -> None:
        completed = subprocess.run(
            ("python3", "scripts/update_fixture_checksums.py", "--check"),
            cwd=ROOT_DIR,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("fixture integrity passed", completed.stdout)

    def test_every_fixture_metadata_records_seed_and_content_hashes(self) -> None:
        for directory in FIXTURE_DIRECTORIES:
            for metadata_path in sorted(directory.glob("*.json")):
                with self.subTest(metadata=metadata_path.name):
                    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
                    self.assertIsInstance(metadata["seed"], int)
                    self.assertEqual(metadata["preamble_seed"], 2029)
                    self.assertTrue(metadata["generator"].startswith("scripts/"))

                    iq_path = metadata_path.with_suffix(".iq")
                    preamble_path = metadata_path.parent / metadata["preamble_file"]
                    self.assertEqual(metadata["iq_sha256"], sha256_file(iq_path))
                    self.assertEqual(
                        metadata["preamble_sha256"],
                        sha256_file(preamble_path.resolve()),
                    )


class BenchmarkProvenanceTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("git"), "Git is needed to test changing revisions")
    def test_incremental_build_reports_current_source_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            parent = Path(temporary)
            source = parent / "source"
            source.mkdir()
            for folder in ("cmake", "src", "tools", "experiments"):
                shutil.copytree(ROOT_DIR / folder, source / folder)
            shutil.copy2(ROOT_DIR / "CMakeLists.txt", source)

            def run(*args: str) -> str:
                return subprocess.run(
                    args, cwd=source, check=True, capture_output=True, text=True
                ).stdout.strip()

            def commit() -> str:
                run("git", "add", ".")
                run(
                    "git", "-c", "user.name=Test", "-c", "user.email=test@example.org",
                    "-c", "commit.gpgSign=false", "commit", "-qm", "Test source",
                )
                return run("git", "rev-parse", "HEAD")

            build = parent / "build"
            executable = "benchmark_acquisition" + (".exe" if os.name == "nt" else "")

            def build_and_read_provenance() -> tuple[str, str]:
                run(
                    "cmake", "--build", str(build), "--config", "Debug",
                    "--target", "benchmark_acquisition", "--parallel", "2",
                )
                binary = next(build.rglob(executable))
                result = run(
                    str(binary), "--workload", "small", "--samples", "3",
                    "--warmup-rounds", "0", "--min-sample-ms", "1",
                )
                report = json.loads(result)
                info = report["benchmark"]
                return info["git_commit_sha"], info["git_working_tree_dirty_at_build"]

            run("git", "init", "-q")
            original = commit()
            run(
                "cmake", "-S", ".", "-B", str(build), "-DBUILD_TESTING=OFF",
                "-DCMAKE_BUILD_TYPE=Debug",
            )
            self.assertEqual(build_and_read_provenance(), (original, "false"))

            with (source / "tools/benchmark_acquisition.cpp").open("a") as tracked:
                tracked.write("\n// Incremental change after configuration.\n")
            self.assertEqual(build_and_read_provenance(), (original, "true"))

            updated = commit()
            self.assertNotEqual(updated, original)
            self.assertEqual(build_and_read_provenance(), (updated, "false"))

            # An unpacked archive must not inherit the containing repository's SHA.
            (source / ".git").rename(parent / ".git")
            self.assertEqual(build_and_read_provenance(), ("unavailable", "unknown"))


if __name__ == "__main__":
    unittest.main()
