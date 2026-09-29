"""Three-package publication behavior through the release publisher seam."""

import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
import publish
from macos import feed_url


BASE = "https://github.com/example/fork/releases"
COMMIT = "a" * 40


class Store:
    def __init__(self):
        self.release = None
        self.objects = {}
        self.actions = []
        self.fail_upload = None

    def tag_commit(self, tag):
        return COMMIT

    def releases(self):
        return [self.release] if self.release else []

    def create(self, tag, notes):
        self.release = dict(
            id=1, tag_name=tag, draft=True, prerelease=False, body=notes.read_text()
        )
        self.actions.append("draft")
        return self.release

    def assets(self, release):
        return self.objects.copy()

    def digest(self, asset):
        return hashlib.sha256(asset).hexdigest()

    def upload(self, tag, path):
        if path.name == self.fail_upload:
            raise ValueError("interrupted")
        self.objects[path.name] = path.read_bytes()
        self.actions.append(path.name)

    def finish(self, tag):
        self.release["draft"] = False
        self.actions.append("publish")

    def available(self, url, digest):
        self.actions.append("public:" + url)


class ThreeAssetPublish(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.store = Store()
        self.version = "1.2.3"
        self.dmg = self.root / "ChoscorDB-1.2.3.dmg"
        self.windows = self.root / "ChoscorDB-1.2.3-windows-x64-setup.exe"
        self.linux = self.root / "ChoscorDB-1.2.3-linux-x86_64.AppImage"
        self.feed = self.root / "choscordb-appcast.xml"
        for path, data in (
            (self.dmg, b"dmg"),
            (self.windows, b"setup"),
            (self.linux, b"appimage"),
        ):
            path.write_bytes(data)
        for path in (self.windows, self.linux):
            (path.parent / (path.name + ".sha256")).write_text(
                hashlib.sha256(path.read_bytes()).hexdigest() + "  " + path.name + "\n"
            )
            (path.parent / (path.name + ".candidate.json")).write_text(
                json.dumps(
                    dict(
                        source_commit=COMMIT,
                        version=self.version,
                        name=path.name,
                        size=path.stat().st_size,
                        sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    )
                )
            )
        self.feed.write_text(
            '<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">'
            "<channel><item><sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion>"
            f'<enclosure url="{BASE}/download/v1.2.3/{self.dmg.name}" '
            'sparkle:version="1.2.3" sparkle:shortVersionString="1.2.3" '
            'sparkle:edSignature="signature" length="3" '
            'type="application/octet-stream"/></item></channel></rss>'
        )
        self.source = self.root / "ChoscorDB-1.2.3-source.tar.gz"
        self.source.write_bytes(b"local source")
        self.notes = self.root / "notes.md"
        self.notes.write_text("Reviewed notes\n")
        self.manifest = dict(
            version=self.version,
            source_commit=COMMIT,
            base_url=BASE,
            feed_url=feed_url(BASE),
            artifacts=[
                dict(
                    path=p.name,
                    size=p.stat().st_size,
                    sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                    role="dmg"
                    if p == self.dmg
                    else "appcast"
                    if p == self.feed
                    else "source",
                )
                for p in (self.dmg, self.feed, self.source)
            ],
        )

    def run_publish(self, **kwargs):
        return publish.publish(
            self.root,
            self.manifest,
            self.store,
            self.windows,
            self.linux,
            notes_file=self.notes,
            **kwargs,
        )

    def test_three_packages_stage_before_publish_and_retry_is_idempotent(self):
        self.run_publish()
        self.assertEqual(
            self.store.actions[:5],
            ["draft", self.dmg.name, self.windows.name, self.linux.name, "publish"],
        )
        self.assertEqual(
            set(self.store.objects), {self.dmg.name, self.windows.name, self.linux.name}
        )
        before = list(self.store.actions)
        self.run_publish()
        self.assertEqual(self.store.actions[:5], before[:5])
        self.assertEqual(self.store.actions.count("publish"), 1)

    def test_interrupted_upload_stays_draft_and_retry_resumes(self):
        self.store.fail_upload = self.linux.name
        with self.assertRaisesRegex(ValueError, "interrupted"):
            self.run_publish()
        self.assertTrue(self.store.release["draft"])
        self.assertNotIn("publish", self.store.actions)
        self.store.fail_upload = None
        self.run_publish()
        self.assertEqual(self.store.actions.count(self.dmg.name), 1)
        self.assertEqual(self.store.actions.count(self.windows.name), 1)

    def test_conflicting_candidate_checksum_refuses_mutation(self):
        self.windows.write_bytes(b"tampered")
        (self.windows.parent / (self.windows.name + ".sha256")).write_text(
            "0" * 64 + "  " + self.windows.name + "\n"
        )
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.run_publish()
        self.assertEqual(self.store.actions, [])

    def test_conflicting_draft_asset_refuses_mutation(self):
        self.store.release = dict(
            id=1,
            tag_name="v1.2.3",
            draft=True,
            prerelease=False,
            body="Reviewed notes\n",
        )
        self.store.objects[self.dmg.name] = b"different DMG"
        with self.assertRaisesRegex(ValueError, "immutable artifact conflict"):
            self.run_publish()
        self.assertEqual(self.store.actions, [])

    def test_public_download_failure_keeps_complete_release_public(self):
        class Unavailable(Store):
            def available(self, url, digest):
                raise ValueError("network outage")

        self.store = Unavailable()
        with self.assertRaisesRegex(ValueError, "Release is published"):
            self.run_publish()
        self.assertFalse(self.store.release["draft"])
        self.assertEqual(
            set(self.store.objects), {self.dmg.name, self.windows.name, self.linux.name}
        )

    def test_platform_candidate_from_other_commit_cannot_stage(self):
        receipt = self.windows.parent / (self.windows.name + ".candidate.json")
        data = json.loads(receipt.read_text())
        data["source_commit"] = "b" * 40
        receipt.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "source receipt mismatch"):
            self.run_publish()
        self.assertEqual(self.store.actions, [])


if __name__ == "__main__":
    unittest.main()
