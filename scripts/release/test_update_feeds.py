"""Signed update feeds and the post-publication Pages gate."""

import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).parent))
import update_feeds


class SignerTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.private = self.root / "key.pem"
        subprocess.run(
            ["openssl", "genpkey", "-algorithm", "Ed25519", "-out", str(self.private)],
            check=True,
            capture_output=True,
        )
        self.public = update_feeds.public_from_private(self.private)

    def test_payload_signature_covers_version_asset_hash_and_notes(self):
        package = self.root / "ChoscorDB-1.2.3-windows-x64-setup.exe"
        package.write_bytes(b"installer bytes")
        envelope = update_feeds.signed_metadata(
            "1.2.3",
            "windows",
            "x64",
            package,
            "https://github.com/example/fork/releases",
            "New SQL tools",
            self.private,
            self.public,
        )
        record = update_feeds.verify_metadata(envelope, self.public)
        self.assertEqual(
            record,
            {
                "version": "1.2.3",
                "platform": "windows",
                "arch": "x64",
                "url": "https://github.com/example/fork/releases/download/v1.2.3/"
                "ChoscorDB-1.2.3-windows-x64-setup.exe",
                "size": len(b"installer bytes"),
                "sha256": hashlib.sha256(b"installer bytes").hexdigest(),
                "notes": "New SQL tools",
            },
        )
        tampered = json.loads(envelope)
        data = json.loads(base64.b64decode(tampered["payload"]))
        data["size"] += 1
        tampered["payload"] = base64.b64encode(
            json.dumps(data, sort_keys=True, separators=(",", ":")).encode()
        ).decode()
        with self.assertRaisesRegex(ValueError, "signature"):
            update_feeds.verify_metadata(json.dumps(tampered).encode(), self.public)

    def test_different_private_key_is_rejected(self):
        other = self.root / "other.pem"
        subprocess.run(
            ["openssl", "genpkey", "-algorithm", "Ed25519", "-out", str(other)],
            check=True,
            capture_output=True,
        )
        package = self.root / "ChoscorDB-1.2.3-linux-x86_64.AppImage"
        package.write_bytes(b"appimage")
        with self.assertRaisesRegex(ValueError, "dedicated update key"):
            update_feeds.signed_metadata(
                "1.2.3",
                "linux",
                "x86_64",
                package,
                "https://github.com/example/fork/releases",
                "Notes",
                other,
                self.public,
            )

    def test_key_creation_never_overwrites_an_existing_private_key(self):
        path = self.root / "fresh.pem"
        created = update_feeds.create_private_key(path)
        self.assertEqual(created, update_feeds.public_from_private(path))
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        original = path.read_bytes()
        with self.assertRaisesRegex(ValueError, "already exists"):
            update_feeds.create_private_key(path)
        self.assertEqual(path.read_bytes(), original)


class FakeStore:
    def __init__(self, names, payloads):
        self.names = names
        self.payloads = payloads
        self.calls = []

    def tag_commit(self, tag):
        return "a" * 40

    def releases(self):
        return [
            dict(
                id=1,
                tag_name="v1.2.3",
                draft=False,
                prerelease=False,
                body="Release notes",
            )
        ]

    def assets(self, release):
        return self.payloads

    def digest(self, asset):
        return hashlib.sha256(asset).hexdigest()

    def available(self, url, checksum):
        self.calls.append((url, checksum))


class FakePages:
    def __init__(self, appcast):
        self.objects = {"choscordb-appcast.xml": appcast}
        self.published = []
        self.fail = False

    def fetch(self, url):
        return self.objects.get(url.rsplit("/", 1)[-1])

    def publish(self, repo, files):
        self.published.append(dict(files))
        if not self.fail:
            self.objects.update(files)


class DeploymentTests(SignerTests):
    def setUp(self):
        super().setUp()
        self.base = "https://github.com/example/fork/releases"
        self.dmg = self.root / "ChoscorDB-1.2.3.dmg"
        self.win = self.root / "ChoscorDB-1.2.3-windows-x64-setup.exe"
        self.lin = self.root / "ChoscorDB-1.2.3-linux-x86_64.AppImage"
        self.feed = self.root / "choscordb-appcast.xml"
        for path, data in (
            (self.dmg, b"dmg"),
            (self.win, b"setup"),
            (self.lin, b"image"),
        ):
            path.write_bytes(data)
        self.feed.write_text(
            '<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">'
            "<channel><item><sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion>"
            f'<enclosure url="{self.base}/download/v1.2.3/{self.dmg.name}" '
            'sparkle:version="1.2.3" sparkle:shortVersionString="1.2.3" '
            'sparkle:edSignature="signature" length="3" '
            'type="application/octet-stream"/></item></channel></rss>'
        )
        for path in (self.win, self.lin):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            (self.root / (path.name + ".sha256")).write_text(f"{digest}  {path.name}\n")
            (self.root / (path.name + ".candidate.json")).write_text(
                json.dumps(
                    dict(
                        source_commit="a" * 40,
                        version="1.2.3",
                        name=path.name,
                        sha256=digest,
                        size=path.stat().st_size,
                    )
                )
            )
        self.manifest = dict(
            version="1.2.3",
            source_commit="a" * 40,
            base_url=self.base,
            feed_url="https://example.github.io/fork/updates/choscordb-appcast.xml",
            artifacts=[
                dict(
                    path=p.name,
                    size=p.stat().st_size,
                    sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
                )
                for p in (self.dmg, self.feed)
            ],
        )
        self.store = FakeStore(
            [p.name for p in (self.dmg, self.win, self.lin)],
            {p.name: p.read_bytes() for p in (self.dmg, self.win, self.lin)},
        )
        old = self.feed.read_bytes().replace(b"1.2.3", b"1.2.2")
        self.pages = FakePages(old)

    def deploy(self):
        update_feeds.deploy(
            self.root,
            self.manifest,
            self.win,
            self.lin,
            "Release notes",
            self.private,
            self.store,
            self.pages,
            self.public,
        )

    def test_pages_advance_only_after_public_three_asset_verification(self):
        self.deploy()
        self.assertEqual(
            set(self.pages.published[0]),
            {"choscordb-appcast.xml", "windows-x64.json", "linux-x86_64.json"},
        )
        self.assertEqual(len(self.store.calls), 3)
        record = update_feeds.verify_metadata(
            self.pages.objects["windows-x64.json"], self.public
        )
        self.assertEqual(record["version"], "1.2.3")

    def test_missing_public_asset_blocks_pages_mutation(self):
        self.store.payloads.pop(self.win.name)
        with self.assertRaisesRegex(ValueError, "exactly three"):
            self.deploy()
        self.assertEqual(self.pages.published, [])

    def test_pages_failure_reports_published_release_and_retry(self):
        self.pages.fail = True
        with self.assertRaisesRegex(ValueError, "Release remains published"):
            self.deploy()
        self.pages.fail = False
        self.deploy()
        self.assertEqual(len(self.pages.published), 2)

    def test_bootstrap_verifies_old_dmg_before_first_pages_write(self):
        old = self.feed.read_bytes().replace(b"1.2.3", b"0.1.7")
        old = old.replace(b'length="3"', b'length="7"')
        appcast = self.root / "old.xml"
        appcast.write_bytes(old)
        site = FakePages(b"")
        site.objects.clear()
        calls = []

        def external(argv, **kwargs):
            calls.append(argv[0])
            if argv[0] == "curl":
                Path(argv[argv.index("--output") + 1]).write_bytes(b"old DMG")
            return SimpleNamespace(returncode=0)

        with patch("update_feeds.subprocess.run", side_effect=external):
            update_feeds.bootstrap(self.base, appcast, site, sparkle_tools=self.root)
        self.assertEqual(calls, ["curl", self.root.resolve() / "bin/sign_update"])
        self.assertEqual(site.objects["choscordb-appcast.xml"], old)
        site.objects.clear()
        appcast.write_bytes(old.replace(b'length="7"', b'length="8"'))
        with patch("update_feeds.subprocess.run", side_effect=external):
            with self.assertRaisesRegex(ValueError, "declared length"):
                update_feeds.bootstrap(
                    self.base, appcast, site, sparkle_tools=self.root
                )
        self.assertEqual(site.objects, {})


class PagesApiRetryTests(unittest.TestCase):
    def test_bootstrap_recovers_pages_creation_race_after_branch_write(self):
        class Site(update_feeds.GitHubPages):
            def __init__(self):
                super().__init__("example/fork")
                self.calls = []
                self.page_reads = 0

            def api(self, method, endpoint, body=None, **kwargs):
                self.calls.append((method, endpoint))
                if (method, endpoint) == ("GET", "pages"):
                    self.page_reads += 1
                    return (
                        None
                        if self.page_reads == 1
                        else {
                            "source": {"branch": "gh-pages", "path": "/"},
                            "cname": None,
                        }
                    )
                if (method, endpoint) == ("GET", "git/ref/heads/gh-pages"):
                    return None
                if (method, endpoint) == ("GET", ""):
                    return {"default_branch": "main"}
                if (method, endpoint) == ("GET", "git/ref/heads/main"):
                    return {"object": {"sha": "a" * 40}}
                if (method, endpoint) == ("POST", "git/trees"):
                    return {"sha": "b" * 40}
                if (method, endpoint) == ("POST", "git/commits"):
                    return {"sha": "c" * 40}
                if (method, endpoint) == ("POST", "pages"):
                    if not kwargs.get("conflict_ok"):
                        raise ValueError("GitHub Pages request failed")
                    return {"_conflict": True}
                return {}

        site = Site()
        site.publish("example/fork", {"choscordb-appcast.xml": b"feed"})
        self.assertEqual(site.page_reads, 2)
        self.assertEqual(site.calls.count(("POST", "git/refs")), 1)

    def test_retry_with_identical_branch_bytes_does_not_commit_again(self):
        data = b"feed"
        git_hash = hashlib.sha1(b"blob 4\0feed").hexdigest()

        class Site(update_feeds.GitHubPages):
            def __init__(self):
                super().__init__("example/fork")
                self.calls = []

            def api(self, method, endpoint, body=None, **kwargs):
                self.calls.append((method, endpoint))
                if endpoint == "pages":
                    return {
                        "source": {"branch": "gh-pages", "path": "/"},
                        "cname": None,
                    }
                if endpoint == "git/ref/heads/gh-pages":
                    return {"object": {"sha": "a" * 40}}
                if endpoint == "git/commits/" + "a" * 40:
                    return {"tree": {"sha": "b" * 40}}
                if endpoint.startswith("git/trees/"):
                    return {
                        "truncated": False,
                        "tree": [
                            {
                                "path": "updates/choscordb-appcast.xml",
                                "type": "blob",
                                "sha": git_hash,
                            }
                        ],
                    }
                raise AssertionError("unexpected remote mutation")

        site = Site()
        site.publish("example/fork", {"choscordb-appcast.xml": data})
        self.assertFalse(any(method != "GET" for method, _ in site.calls))


class CrossPlatformImportTests(unittest.TestCase):
    def test_windows_feed_preflight_can_import_release_parser_without_fcntl(self):
        path = Path(__file__).with_name("publish.py")
        spec = importlib.util.spec_from_file_location("publish_without_fcntl", path)
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"fcntl": None}):
            spec.loader.exec_module(module)
        self.assertEqual(module.version_tuple("1.2.3"), (1, 2, 3))


if __name__ == "__main__":
    unittest.main()
