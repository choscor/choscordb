"""Fail-closed checks for adding platform assets to an existing release."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
import attach_platforms


COMMIT = "a" * 40


class Store:
    def __init__(self):
        self.uploaded = {}
        self.release = {
            "id": 1,
            "tag_name": "v1.2.3",
            "draft": True,
            "prerelease": False,
        }
        self.public = []

    def tag_commit(self, tag):
        return COMMIT

    def releases(self):
        return [self.release]

    def assets(self, release):
        return {
            name: {"name": name, "bytes": data} for name, data in self.uploaded.items()
        }

    def digest(self, asset):
        return hashlib.sha256(asset["bytes"]).hexdigest()

    def upload(self, tag, path):
        self.uploaded[path.name] = path.read_bytes()

    def available(self, url, digest):
        self.public.append((url, digest))


class AttachTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.windows = root / "ChoscorDB-1.2.3-windows-x64-setup.exe"
        self.linux = root / "ChoscorDB-1.2.3-linux-x86_64.AppImage"
        self.windows.write_bytes(b"unsigned Windows installer")
        self.linux.write_bytes(b"Linux AppImage")
        for path in (self.windows, self.linux):
            checksum = hashlib.sha256(path.read_bytes()).hexdigest()
            (root / (path.name + ".sha256")).write_text(f"{checksum}  {path.name}\n")
            (root / (path.name + ".candidate.json")).write_text(
                json.dumps(
                    {
                        "source_commit": COMMIT,
                        "version": "1.2.3",
                        "name": path.name,
                        "size": path.stat().st_size,
                        "sha256": checksum,
                    }
                )
            )
        self.store = Store()
        check = patch(
            "attach_platforms.subprocess.check_output",
            side_effect=[COMMIT, COMMIT] * 4,
        )
        check.start()
        self.addCleanup(check.stop)

    def attach(self, **kwargs):
        return attach_platforms.attach(
            "choscor/choscordb",
            "v1.2.3",
            self.windows,
            self.linux,
            store=self.store,
            **kwargs,
        )

    def test_candidates_verify_without_uploading(self):
        receipt = self.attach()
        self.assertEqual(set(receipt), {self.windows.name, self.linux.name})
        self.assertEqual(self.store.uploaded, {})
        self.assertEqual(self.store.public, [])

    def test_missing_asset_stops_before_remote_upload(self):
        self.windows.unlink()
        with self.assertRaisesRegex(ValueError, "missing or unsafe"):
            self.attach()
        self.assertEqual(self.store.uploaded, {})

    def test_public_release_cannot_be_attached(self):
        self.store.release["draft"] = False
        with self.assertRaisesRegex(ValueError, "public or ambiguous"):
            self.attach()
        self.assertEqual(self.store.uploaded, {})


if __name__ == "__main__":
    unittest.main()
