import hashlib
import json
import subprocess
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


if __name__ == "__main__":
    unittest.main()
