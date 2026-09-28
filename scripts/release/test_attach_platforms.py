"""Fail-closed checks for adding platform assets to an existing release."""

import hashlib
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
            "draft": False,
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
        self.windows = root / "ChoscorDB-1.2.3-windows-x64.zip"
        self.linux = root / "ChoscorDB-1.2.3-linux-x86_64.AppImage"
        self.windows.write_bytes(b"unsigned Windows ZIP")
        self.linux.write_bytes(b"Linux AppImage")
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

    def test_uploads_once_and_reuses_only_identical_assets(self):
        self.attach()
        self.assertEqual(len(self.store.uploaded), 3)
        self.assertEqual(len(self.store.public), 3)
        self.attach()
        self.assertEqual(len(self.store.uploaded), 3)
        self.store.uploaded[self.windows.name] = b"conflict"
        with self.assertRaisesRegex(ValueError, "Conflicting published asset"):
            self.attach()

    def test_missing_asset_stops_before_remote_upload(self):
        self.windows.unlink()
        with self.assertRaisesRegex(ValueError, "Both platform assets"):
            self.attach()
        self.assertEqual(self.store.uploaded, {})

    def test_draft_release_is_not_mutated(self):
        self.store.release["draft"] = True
        with self.assertRaisesRegex(ValueError, "published stable"):
            self.attach()
        self.assertEqual(self.store.uploaded, {})


if __name__ == "__main__":
    unittest.main()
