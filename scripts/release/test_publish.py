import importlib
import base64
import json
import plistlib
import io
import tarfile
import os
import subprocess
from unittest.mock import patch
import hashlib
import pathlib
import tempfile
import unittest
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
publish = importlib.import_module("publish")

BASE = "https://github.com/choscor/choscordb/releases"


class Store:
    def __init__(self):
        self.records = []
        self.objects = {}
        self.actions = []
        self.commit = "a" * 40
        self.fail = None

    def tag_commit(self, tag):
        return self.commit

    def releases(self):
        return self.records

    def create(self, tag, notes):
        self.actions.append("draft")
        record = dict(
            id=1,
            tag_name=tag,
            draft=True,
            prerelease=False,
            assets=[],
            body=notes.read_text() if notes else "Reviewed notes\n",
        )
        self.records.append(record)
        return record

    def assets(self, release):
        return self.objects

    def digest(self, asset):
        return hashlib.sha256(asset).hexdigest()

    def upload(self, tag, path):
        if self.fail == path.name:
            raise ValueError("interrupted")
        self.actions.append(path.name)
        self.objects[path.name] = path.read_bytes()

    def finish(self, tag):
        self.actions.append("publish")
        self.records[0]["draft"] = False

    def available(self, url, digest):
        pass


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.store = Store()
        for name in ("notes", "notes.md"):
            (self.root / name).write_text("Reviewed notes\n")

    def release(self, version="1.0.0"):
        name = f"ChoscorDB-{version}.dmg"
        feed = f'<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><item><sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion><enclosure url="{BASE}/download/v{version}/{name}" sparkle:version="{version}" sparkle:shortVersionString="{version}" sparkle:edSignature="signature" length="3" type="application/octet-stream"/></item></channel></rss>'.encode()
        artifacts = []
        for path, data in [
            (name, b"dmg"),
            ("ChoscorDB.dmg", b"dmg"),
            ("choscordb-appcast.xml", feed),
            (f"ChoscorDB-{version}-source.tar.gz", b"local source"),
        ]:
            (self.root / path).write_bytes(data)
            artifacts.append(
                dict(
                    path=path,
                    size=len(data),
                    sha256=hashlib.sha256(data).hexdigest(),
                    role=(
                        "dmg"
                        if path == name
                        else (
                            "latest"
                            if path == "ChoscorDB.dmg"
                            else "appcast"
                            if path.endswith(".xml")
                            else "source"
                        )
                    ),
                )
            )
        return dict(
            version=version,
            source_commit="a" * 40,
            base_url=BASE,
            feed_url=BASE + "/latest/download/choscordb-appcast.xml",
            artifacts=artifacts,
        )

    def test_draft_then_three_assets_then_publish_and_idempotent_retry(self):
        manifest = self.release()
        publish.publish(
            self.root, manifest, self.store, notes_file=self.root / "notes.md"
        )
        self.assertEqual(
            self.store.actions,
            [
                "draft",
                "ChoscorDB-1.0.0.dmg",
                "ChoscorDB.dmg",
                "choscordb-appcast.xml",
                "publish",
            ],
        )
        publish.publish(
            self.root, manifest, self.store, notes_file=self.root / "notes.md"
        )
        self.assertEqual(len(self.store.actions), 5)

    def test_conflicting_release_notes_refuse_before_mutations(self):
        manifest = self.release()
        for draft in (True, False):
            with self.subTest(draft=draft):
                store = Store()
                record = store.create("v1.0.0", self.root / "notes")
                record.update(draft=draft, body="Unreviewed private notes")
                store.objects = {
                    name: (self.root / name).read_bytes()
                    for name in (
                        "ChoscorDB-1.0.0.dmg",
                        "ChoscorDB.dmg",
                        "choscordb-appcast.xml",
                    )
                }
                store.actions.clear()
                with self.assertRaisesRegex(ValueError, "release notes conflict"):
                    publish.publish(
                        self.root, manifest, store, notes_file=self.root / "notes"
                    )
                self.assertEqual(store.actions, [])

    def test_notes_changed_after_final_asset_stay_draft(self):
        class ChangedNotes(Store):
            def upload(self, tag, path):
                super().upload(tag, path)
                if path.name == "choscordb-appcast.xml":
                    self.records[0]["body"] = "Unreviewed replacement"

        store = ChangedNotes()
        with self.assertRaisesRegex(ValueError, "release notes conflict"):
            publish.publish(
                self.root, self.release(), store, notes_file=self.root / "notes"
            )
        self.assertTrue(store.records[0]["draft"])
        self.assertNotIn("publish", store.actions)

    def test_notes_compare_ignores_only_trailing_newlines(self):
        manifest = self.release()
        self.store.create("v1.0.0", self.root / "notes")
        self.store.records[0]["body"] = "Reviewed notes"
        publish.publish(self.root, manifest, self.store, notes_file=self.root / "notes")
        self.assertFalse(self.store.records[0]["draft"])
        self.store.records[0]["body"] = "Reviewed notes "
        self.store.actions.clear()
        with self.assertRaisesRegex(ValueError, "release notes conflict"):
            publish.publish(
                self.root, manifest, self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_dry_run_does_not_write(self):
        publish.publish(self.root, self.release(), self.store, dry_run=True)
        self.assertEqual(self.store.actions, [])

    def test_remote_tag_must_match_exact_commit(self):
        self.store.commit = "b" * 40
        with self.assertRaisesRegex(ValueError, "source commit"):
            publish.publish(
                self.root, self.release(), self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_stale_version_is_rejected(self):
        self.store.records = [
            dict(id=2, tag_name="v2.0.0", draft=False, prerelease=False)
        ]
        with self.assertRaisesRegex(ValueError, "stale"):
            publish.publish(
                self.root, self.release(), self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_partial_draft_resumes_without_reuploading(self):
        manifest = self.release()
        self.store.fail = "ChoscorDB.dmg"
        with self.assertRaisesRegex(ValueError, "interrupted"):
            publish.publish(
                self.root, manifest, self.store, notes_file=self.root / "notes"
            )
        self.assertTrue(self.store.records[0]["draft"])
        self.assertNotIn("choscordb-appcast.xml", self.store.objects)
        self.store.fail = None
        publish.publish(self.root, manifest, self.store, notes_file=self.root / "notes")
        self.assertEqual(
            self.store.actions,
            [
                "draft",
                "ChoscorDB-1.0.0.dmg",
                "ChoscorDB.dmg",
                "choscordb-appcast.xml",
                "publish",
            ],
        )

    def test_conflicting_draft_asset_is_not_clobbered(self):
        manifest = self.release()
        self.store.create("v1.0.0", None)
        self.store.objects["ChoscorDB-1.0.0.dmg"] = b"other"
        self.store.actions.clear()
        with self.assertRaisesRegex(ValueError, "immutable"):
            publish.publish(
                self.root, manifest, self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_unpublished_source_is_still_verified(self):
        manifest = self.release()
        (self.root / "ChoscorDB-1.0.0-source.tar.gz").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            publish.publish(
                self.root, manifest, self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_public_verification_failure_reports_published_state(self):
        class Unavailable(Store):
            def available(self, url, digest):
                raise ValueError("unavailable")

        store = Unavailable()
        with self.assertRaisesRegex(ValueError, "Release is published"):
            publish.publish(
                self.root, self.release(), store, notes_file=self.root / "notes"
            )
        self.assertFalse(store.records[0]["draft"])

    def test_finalization_failure_reports_uncertain_remote_state(self):
        class Interrupted(Store):
            def finish(self, tag):
                super().finish(tag)
                raise ValueError("network timeout")

        store = Interrupted()
        with self.assertRaisesRegex(ValueError, "may already be published"):
            publish.publish(
                self.root, self.release(), store, notes_file=self.root / "notes"
            )

    def test_tag_changed_during_upload_stops_before_alias_and_feed(self):
        class ChangedTag(Store):
            def upload(self, tag, path):
                super().upload(tag, path)
                self.commit = "b" * 40

        store = ChangedTag()
        with self.assertRaisesRegex(ValueError, "source commit"):
            publish.publish(
                self.root, self.release(), store, notes_file=self.root / "notes"
            )
        self.assertEqual(store.actions, ["draft", "ChoscorDB-1.0.0.dmg"])
        self.assertTrue(store.records[0]["draft"])

    def test_corrupt_upload_stops_before_next_asset(self):
        class Corrupt(Store):
            def upload(self, tag, path):
                super().upload(tag, path)
                self.objects[path.name] = b"corrupt"

        store = Corrupt()
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            publish.publish(
                self.root, self.release(), store, notes_file=self.root / "notes"
            )
        self.assertEqual(store.actions, ["draft", "ChoscorDB-1.0.0.dmg"])
        self.assertTrue(store.records[0]["draft"])

    def test_unexpected_assets_refuse_mutations(self):
        self.store.create("v1.0.0", None)
        self.store.objects["private-source.tar.gz"] = b"private"
        self.store.actions.clear()
        with self.assertRaisesRegex(ValueError, "unexpected assets"):
            publish.publish(
                self.root, self.release(), self.store, notes_file=self.root / "notes"
            )
        self.assertEqual(self.store.actions, [])

    def test_overlapping_local_publisher_refuses(self):
        with patch("tempfile.gettempdir", return_value=str(self.root)):
            with publish.publication_lock():
                with self.assertRaisesRegex(ValueError, "another local"):
                    with publish.publication_lock():
                        self.fail("acquired overlapping lock")

    def test_github_cli_transport_and_authenticated_asset_digest(self):
        executable = self.root / "gh"
        executable.write_text(
            "#!"
            + sys.executable
            + "\n"
            + r"""import json, os, pathlib, sys
args=sys.argv[1:]
with open(os.environ['COMMAND_LOG'],'a') as log: log.write(json.dumps(args)+'\n')
if os.environ.get('DENIED'):
    print('private-token-details',file=sys.stderr);sys.exit(1)
if args[0]=='api':
    endpoint=args[1]
    if 'git/ref/tags/' in endpoint: print(json.dumps({'object':{'type':'tag','sha':'b'*40}}))
    elif 'git/tags/' in endpoint: print(json.dumps({'object':{'type':'commit','sha':'a'*40}}))
    elif '/releases/assets/' in endpoint:
        assert args[-2:]==['-H','Accept: application/octet-stream']
        sys.stdout.buffer.write(b'dmg')
    elif '/assets?' in endpoint: print(json.dumps([{'name':'ChoscorDB-1.0.0.dmg','id':7}]))
    elif 'releases?' in endpoint: print('[]')
    elif '/releases/tags/' in endpoint: print(json.dumps({'id':1,'draft':True,'prerelease':False,'tag_name':'v1.0.0'}))
    else: sys.exit(99)
"""
        )
        executable.chmod(0o755)
        log = self.root / "log"
        with patch.dict(
            os.environ,
            {
                "PATH": str(self.root) + os.pathsep + os.environ["PATH"],
                "COMMAND_LOG": str(log),
            },
        ):
            store = publish.GitHubStore("choscor/choscordb")
            self.assertEqual(store.tag_commit("v1.0.0"), "a" * 40)
            self.assertEqual(store.releases(), [])
            release = store.create("v1.0.0", self.root / "notes.md")
            asset = store.assets(release)["ChoscorDB-1.0.0.dmg"]
            self.assertEqual(store.digest(asset), hashlib.sha256(b"dmg").hexdigest())
            store.upload("v1.0.0", self.root / "ChoscorDB-1.0.0.dmg")
            store.finish("v1.0.0")
            commands = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertIn(
                [
                    "release",
                    "edit",
                    "v1.0.0",
                    "--repo",
                    "choscor/choscordb",
                    "--draft=false",
                    "--latest",
                ],
                commands,
            )
            self.assertTrue(
                any(
                    "--verify-tag" in command and "--draft" in command
                    for command in commands
                )
            )
            self.assertFalse(any("--clobber" in command for command in commands))
            with patch.dict(os.environ, {"DENIED": "1"}):
                with self.assertRaisesRegex(
                    ValueError, "GitHub request failed"
                ) as caught:
                    store.releases()
                self.assertNotIn("private-token-details", str(caught.exception))

    def test_curl_checks_https_redirects_and_redacts_failure(self):
        executable = self.root / "curl"
        executable.write_text(
            "#!"
            + sys.executable
            + "\n"
            + r"""import os,pathlib,sys
args=sys.argv[1:]
assert '--fail' in args and '--location' in args
assert args[args.index('--proto-redir')+1]=='=https'
if os.environ.get('DENIED'):
    print('private-details',file=sys.stderr);sys.exit(22)
pathlib.Path(args[args.index('--output')+1]).write_bytes(b'dmg')
"""
        )
        executable.chmod(0o755)
        with patch.dict(
            os.environ, {"PATH": str(self.root) + os.pathsep + os.environ["PATH"]}
        ):
            store = publish.GitHubStore("choscor/choscordb")
            store.available(
                BASE + "/latest/download/ChoscorDB.dmg",
                hashlib.sha256(b"dmg").hexdigest(),
            )
            with self.assertRaisesRegex(ValueError, "bytes differ"):
                store.available(BASE + "/latest/download/ChoscorDB.dmg", "wrong")
            with patch.dict(os.environ, {"DENIED": "1"}):
                with self.assertRaisesRegex(
                    ValueError, "public download failed"
                ) as caught:
                    store.available(BASE + "/latest/download/ChoscorDB.dmg", "wrong")
                self.assertNotIn("private-details", str(caught.exception))

    def test_full_cli_dry_run_verifies_before_store_and_rejects_tampering(self):
        # Executable fixtures exercise the real CLI and validation wiring only;
        # these are deliberately not evidence of actual Apple notarization.
        manifest = self.release()
        public = base64.b64encode(bytes(range(32))).decode()
        signature = base64.b64encode(bytes(range(64))).decode()
        feed_path = self.root / "choscordb-appcast.xml"
        feed_path.write_text(
            feed_path.read_text()
            .replace('edSignature="signature"', f'edSignature="{signature}"')
            .replace(
                "<enclosure",
                "<sparkle:version>1.0.0</sparkle:version><sparkle:shortVersionString>1.0.0</sparkle:shortVersionString><enclosure",
            )
        )
        metadata = self.root / "ChoscorDB-1.0.0-notices.txt"
        metadata.write_text("fixture notices")
        manifest["artifacts"].append(
            dict(
                path=metadata.name,
                role="metadata",
                size=metadata.stat().st_size,
                sha256=hashlib.sha256(metadata.read_bytes()).hexdigest(),
            )
        )
        for artifact in manifest["artifacts"]:
            data = (self.root / artifact["path"]).read_bytes()
            artifact.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        app = self.root / "staged/ChoscorDB.app"
        for name in [
            "Contents/MacOS/choscordb",
            "Contents/PlugIns/platforms/libqcocoa.dylib",
        ]:
            binary = app / name
            binary.parent.mkdir(parents=True, exist_ok=True)
            binary.write_bytes(bytes.fromhex("cffaedfe") + b"fixture")
            binary.chmod(0o755)
        (app / "Contents/Info.plist").write_bytes(
            plistlib.dumps(
                dict(
                    CFBundleIdentifier="com.choscor.ChoscorDB",
                    CFBundleShortVersionString="1.0.0",
                    CFBundleVersion="1.0.0",
                    CFBundleIconFile="AppIcon.icns",
                    LSMinimumSystemVersion="26.0",
                    SUPublicEDKey=public,
                    SUFeedURL=manifest["feed_url"],
                    SUAllowsAutomaticUpdates=False,
                )
            )
        )
        (app / "Contents/Resources").mkdir(exist_ok=True)
        (app / "Contents/Resources/AppIcon.icns").write_bytes(
            b"icns" + (12).to_bytes(4, "big") + b"test"
        )
        for framework, version in [("QtCore", "6.8.3"), ("Sparkle", "2.9.6")]:
            info = (
                app / f"Contents/Frameworks/{framework}.framework/Resources/Info.plist"
            )
            info.parent.mkdir(parents=True)
            info.write_bytes(
                plistlib.dumps(
                    {
                        "CFBundleShortVersionString": (
                            "6.8" if framework == "QtCore" else version
                        ),
                        "CFBundleVersion": version,
                    }
                )
            )
        for dependency, record in [
            (
                "QScintilla",
                {
                    "version": "2.14.1",
                    "qt_version": "6.8.3",
                    "sha256": "dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d",
                },
            ),
            (
                "Sparkle",
                {
                    "version": "2.9.6",
                    "sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
                },
            ),
        ]:
            notice = app / f"Contents/Resources/licenses/{dependency}/source.json"
            notice.parent.mkdir(parents=True)
            notice.write_text(json.dumps(record))
        inventory = self.root / "ChoscorDB-1.0.0-sbom.spdx.json"
        inventory.write_text(
            json.dumps(
                {
                    "files": [
                        {
                            "fileName": "./" + entry.relative_to(app.parent).as_posix(),
                            "checksums": [
                                {
                                    "algorithm": "SHA256",
                                    "checksumValue": hashlib.sha256(
                                        entry.read_bytes()
                                    ).hexdigest(),
                                }
                            ],
                        }
                        for entry in sorted(app.rglob("*"))
                        if entry.is_file()
                    ]
                }
            )
        )
        manifest["artifacts"].append(
            dict(
                path=inventory.name,
                role="metadata",
                size=inventory.stat().st_size,
                sha256=hashlib.sha256(inventory.read_bytes()).hexdigest(),
            )
        )
        for suffix, role in [
            ("source.json", "metadata"),
            ("THIRD_PARTY_NOTICES.md", "metadata"),
            ("licenses.tar.gz", "metadata"),
            ("cargo-source.tar.gz", "source"),
            ("qtbase-6.8.3.tar.xz", "source"),
            ("qtsvg-6.8.3.tar.xz", "source"),
            ("QScintilla_src-2.14.1.tar.gz", "source"),
            ("SHA256SUMS", "metadata"),
        ]:
            artifact = self.root / ("ChoscorDB-1.0.0-" + suffix)
            artifact.write_bytes(b"controlled fixture metadata/source")
            manifest["artifacts"].append(
                dict(
                    path=artifact.name,
                    role=role,
                    size=artifact.stat().st_size,
                    sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                )
            )
        files = {
            "Cargo.toml": b'[workspace.package]\nversion = "1.0.0"\n',
            "CHANGELOG.md": b"# Changes\n\n## 1.0.0\nRelease\n",
        }
        source = {
            "format_version": 1,
            "git_revision": "a" * 40,
            "files": [
                {
                    "path": name,
                    "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
                for name, data in files.items()
            ],
        }
        (self.root / "ChoscorDB-1.0.0-source.json").write_text(json.dumps(source))
        with tarfile.open(
            self.root / "ChoscorDB-1.0.0-source.tar.gz", "w:gz"
        ) as archive:
            for name, data in files.items():
                member = tarfile.TarInfo("choscordb-source/" + name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        for artifact in manifest["artifacts"]:
            data = (self.root / artifact["path"]).read_bytes()
            artifact.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        sums = self.root / "ChoscorDB-1.0.0-SHA256SUMS"
        sums.write_text(
            "".join(
                f"{a['sha256']}  {a['path']}\n"
                for a in manifest["artifacts"]
                if a["path"] != sums.name
            )
        )
        next(a for a in manifest["artifacts"] if a["path"] == sums.name).update(
            size=sums.stat().st_size,
            sha256=hashlib.sha256(sums.read_bytes()).hexdigest(),
        )
        manifest["dependencies"] = {
            "Qt": "6.8.3",
            "QScintilla": "2.14.1",
            "Sparkle": "2.9.6",
            "Sparkle_sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
            "minimum_macos": "26.0",
            "architecture": "arm64",
        }
        tools = self.root / "tools/bin"
        tools.mkdir(parents=True)
        log = self.root / "commands.log"
        script = r"""#!{python}
import os, pathlib, shutil, sys
name = pathlib.Path(sys.argv[0]).name
with open(os.environ['FIXTURE_LOG'], 'a') as log: log.write(name + ' ' + ' '.join(sys.argv[1:]) + '\n')
if name == 'generate_keys': print({public!r})
if name == 'lipo': print('arm64')
if name == 'otool' and '-l' in sys.argv: print('Load command 0\n cmd LC_BUILD_VERSION\n platform 1\n minos 26.0\n sdk 26.5')
if name == 'hdiutil' and 'attach' in sys.argv:
    mount = pathlib.Path(sys.argv[sys.argv.index('-mountpoint') + 1])
    shutil.copytree(os.environ['FIXTURE_APP'], mount / 'ChoscorDB.app')
    (mount / 'Applications').symlink_to('/Applications')
    if os.environ.get('FIXTURE_BAD_MOUNT'):
        (mount / 'ChoscorDB.app/Contents/MacOS/choscordb').write_bytes(b'different app')
if name == 'codesign' and '-d' in sys.argv: print('Authority=Developer ID Application: Fixture\nCodeDirectory v=20500 size=800 flags=0x10000(runtime) hashes=10', file=sys.stderr)
if name == 'gh':
    assert sys.argv[1] == 'api'
    endpoint = sys.argv[2]
    if 'git/ref/tags/' in endpoint: print('{{"object":{{"type":"commit","sha":"' + 'a'*40 + '"}}}}')
    elif 'releases?' in endpoint: print('[]')
    else: sys.exit(99)
""".format(python=os.sys.executable, public=public)
        for name in [
            "generate_keys",
            "sign_update",
            "lipo",
            "otool",
            "codesign",
            "xcrun",
            "spctl",
            "hdiutil",
            "gh",
        ]:
            executable = tools / name
            executable.write_text(script)
            executable.chmod(0o755)
        manifest.update(
            format_version=1,
            production=True,
            status="verified",
            source_commit="a" * 40,
            app_path="staged/ChoscorDB.app",
            sparkle_public_key=public,
            verification={
                key: True
                for key in [
                    "app_signature",
                    "app_notarized",
                    "dmg_signature",
                    "dmg_notarized",
                    "relocation_smoke",
                    "payload_inventory",
                    "arm64_minimum_os",
                ]
            },
        )
        path = self.root / "release.json"
        path.write_text(json.dumps(manifest))
        command = [
            os.sys.executable,
            publish.__file__,
            "--manifest",
            str(path),
            "--dry-run",
        ]
        env = {
            **os.environ,
            "PATH": str(tools) + os.pathsep + os.environ["PATH"],
            "FIXTURE_LOG": str(log),
            "FIXTURE_APP": str(app),
            "CHOSCORDB_SPARKLE_TOOLS": str(tools.parent),
        }
        # The unmodified production CLI must reject tiny synthetic native archives,
        # even though their fixture manifest/checksums agree with their bytes.
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("native source does not match reviewed pin", result.stdout)
        self.assertFalse(log.exists())
        # For orchestration coverage only, substitute expected hash DATA in this
        # subprocess, never a production CLI flag or environment override. Real
        # upstream archives and Apple signatures are not claimed by this fixture.
        launcher = self.root / "fixture_launcher.py"
        fixture_hash = hashlib.sha256(b"controlled fixture metadata/source").hexdigest()
        launcher.write_text(
            "import sys\n"
            + "sys.path.insert(0, "
            + repr(str(pathlib.Path(publish.__file__).parent))
            + ")\n"
            + "import release_consistency, publish, macos\n"
            + "release_consistency.NATIVE_SOURCE_HASHES = {name: "
            + repr(fixture_hash)
            + " for name in release_consistency.NATIVE_SOURCE_HASHES}\n"
            + "if sys.argv[1] == 'verify': raise SystemExit(macos.main())\n"
            + "raise SystemExit(publish.main(sys.argv[1:]))\n"
        )
        command[1] = str(launcher)
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("ChoscorDB-1.0.0-manifest.json", result.stdout)
        self.assertNotIn("source.tar.gz", result.stdout)
        self.assertIn(
            "https://github.com/choscor/choscordb/releases/download/v1.0.0/ChoscorDB-1.0.0.dmg",
            result.stdout,
        )
        commands = log.read_text()
        self.assertIn("stapler validate", commands)
        self.assertIn("sign_update --account com.choscor.ChoscorDB --verify", commands)
        self.assertIn("gh api repos/choscor/choscordb/git/ref/tags/v1.0.0", commands)
        self.assertNotIn("gh release", commands)
        # Explicit local configuration wins over an unrelated local environment.
        env["CHOSCORDB_SPARKLE_TOOLS"] = str(self.root / "wrong-tools")
        command.extend(["--sparkle-tools", str(tools.parent)])
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        verify_command = [
            os.sys.executable,
            str(launcher),
            "verify",
            "--manifest",
            str(path),
            "--sparkle-tools",
            str(tools.parent),
        ]
        result = subprocess.run(verify_command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        verified = json.loads(result.stdout)
        self.assertNotIn("sparkle_tools", verified)
        self.assertNotIn(str(tools.parent), result.stdout)
        self.assertNotIn(str(self.root), result.stdout)
        leaked_resource = app / "Contents/Resources/local-build.txt"
        leaked_resource.write_text(str(pathlib.Path.home() / "private-source"))
        log.write_text("")
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("local build path", result.stdout)
        self.assertNotIn(str(pathlib.Path.home()), result.stdout)
        self.assertEqual(log.read_text(), "")
        leaked_resource.unlink()
        # A legacy field is rejected before any executable can be selected from it.
        path.write_text(
            json.dumps(
                {**manifest, "sparkle_tools": str(self.root / "untrusted-tools")}
            )
        )
        log.write_text("")
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("local tool paths", result.stdout)
        self.assertEqual(log.read_text(), "")
        path.write_text(json.dumps(manifest))
        for field, replacement, diagnostic in [
            ("source_commit", "b" * 40, "source commit"),
            (
                "dependencies",
                {**manifest["dependencies"], "Qt": "99.0.0"},
                "dependency pins",
            ),
        ]:
            path.write_text(json.dumps({**manifest, field: replacement}))
            log.write_text("")
            result = subprocess.run(command, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(diagnostic, result.stdout)
            self.assertEqual(log.read_text(), "")
        path.write_text(json.dumps(manifest))
        result = subprocess.run(
            command,
            env={**env, "FIXTURE_BAD_MOUNT": "1"},
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("DMG app differs", result.stdout)
        (self.root / "ChoscorDB-1.0.0.dmg").write_bytes(b"tampered")
        log.write_text("")
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("artifact changed", result.stdout)
        self.assertEqual(log.read_text(), "")


if __name__ == "__main__":
    unittest.main()
