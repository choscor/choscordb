import importlib.util
import pathlib
import tempfile
import unittest
import hashlib
import base64
import json
import plistlib
import io
import tarfile
import os
import subprocess
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "publish", pathlib.Path(__file__).with_name("publish.py")
)
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class Store:
    def __init__(self):
        self.objects = {}
        self.actions = []
        self.unavailable = False

    def get(self, key):
        return self.objects.get(key)

    def put(self, key, data):
        self.actions.append(key)
        self.objects[key] = data

    def available(self, key, data):
        if self.unavailable:
            raise ValueError("unavailable")
        if self.objects.get(key) != data:
            raise ValueError("wrong public bytes")


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.store = Store()

    def release(self, version="1.0.0"):
        name = f"ChoscorDB-{version}.dmg"
        dmg_bytes = ("dmg-" + version).encode()
        feed = f'<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle"><channel><item><sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion><enclosure url="https://cdn.choscor.com/{name}" sparkle:version="{version}" sparkle:shortVersionString="{version}" sparkle:edSignature="signature" length="{len(dmg_bytes)}" type="application/octet-stream"/></item></channel></rss>'.encode()
        artifacts = []
        for path, data, role in [
            (name, dmg_bytes, "dmg"),
            ("ChoscorDB.dmg", dmg_bytes, "latest"),
            ("choscordb-appcast.xml", feed, "appcast"),
            (f"ChoscorDB-{version}-source.tar.gz", b"src", "source"),
        ]:
            (self.root / path).write_bytes(data)
            artifacts.append(
                dict(
                    path=path,
                    sha256=hashlib.sha256(data).hexdigest(),
                    size=len(data),
                    role=role,
                )
            )
        return dict(
            version=version,
            base_url="https://cdn.choscor.com",
            feed_url="https://cdn.choscor.com/choscordb-appcast.xml",
            artifacts=artifacts,
        )

    def test_dry_run_has_no_writes(self):
        publish.publish(self.root, self.release(), self.store, dry_run=True)
        self.assertEqual(self.store.actions, [])

    def test_retry_and_feed_last(self):
        manifest = self.release()
        publish.publish(self.root, manifest, self.store)
        self.assertEqual(
            self.store.actions[-2:], ["ChoscorDB.dmg", "choscordb-appcast.xml"]
        )
        publish.publish(self.root, manifest, self.store)

    def test_conflict_rejected_before_any_write(self):
        manifest = self.release()
        self.store.objects["ChoscorDB-1.0.0.dmg"] = b"other"
        with self.assertRaisesRegex(ValueError, "immutable"):
            publish.publish(self.root, manifest, self.store)
        self.assertEqual(self.store.actions, [])

    def test_stale_rejected_and_history_preserved(self):
        publish.publish(self.root, self.release(), self.store)
        publish.publish(self.root, self.release("2.0.0"), self.store)
        self.assertIn(b"1.0.0", self.store.objects["choscordb-appcast.xml"])
        with self.assertRaisesRegex(ValueError, "stale"):
            publish.publish(self.root, self.release(), self.store)

    def test_unavailable_never_exposes_feed(self):
        self.store.unavailable = True
        with self.assertRaisesRegex(ValueError, "unavailable"):
            publish.publish(self.root, self.release(), self.store)
        self.assertNotIn("choscordb-appcast.xml", self.store.objects)

    def test_changed_payload_rejected(self):
        manifest = self.release()
        (self.root / "ChoscorDB-1.0.0.dmg").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "hash"):
            publish.publish(self.root, manifest, self.store)
        self.assertEqual(self.store.actions, [])

    def test_partial_failure_retry(self):
        manifest = self.release()
        original = self.store.put

        def failing(key, data):
            if key.endswith("source.tar.gz"):
                raise ValueError("interrupted upload")
            original(key, data)

        self.store.put = failing
        with self.assertRaisesRegex(ValueError, "interrupted"):
            publish.publish(self.root, manifest, self.store)
        self.assertNotIn("choscordb-appcast.xml", self.store.objects)
        self.store.put = original
        publish.publish(self.root, manifest, self.store)
        self.assertIn("choscordb-appcast.xml", self.store.objects)

    def test_overlapping_publication_is_rejected(self):
        with publish.publication_lock():
            with self.assertRaisesRegex(ValueError, "another local"):
                with publish.publication_lock():
                    self.fail("overlapping lock acquired")

    def test_network_failure_is_not_absence(self):
        def failure(key):
            raise ValueError("authentication failure")

        self.store.get = failure
        with self.assertRaisesRegex(ValueError, "authentication"):
            publish.publish(self.root, self.release(), self.store)
        self.assertEqual(self.store.actions, [])

    def test_interrupted_newer_alias_blocks_old_retry(self):
        publish.publish(self.root, self.release(), self.store)
        self.store.objects["ChoscorDB.dmg"] = b"newer release bytes"
        with self.assertRaisesRegex(ValueError, "latest alias"):
            publish.publish(self.root, self.release(), self.store)

    def test_r2_tool_distinguishes_missing_from_denied(self):
        executable = self.root / "aws"
        executable.write_text(
            '#!/bin/sh\nprintf "An error occurred (NoSuchKey)" >&2\nexit 1\n'
        )
        executable.chmod(0o755)
        store = publish.R2Store(
            "choscor-downloads", "https://fixture.invalid", "https://fixture.invalid"
        )
        with patch.dict(
            os.environ, {"PATH": str(self.root) + os.pathsep + os.environ["PATH"]}
        ):
            self.assertIsNone(store.get("missing"))
            executable.write_text(
                '#!/bin/sh\nprintf "An error occurred (AccessDenied) private-details" >&2\nexit 1\n'
            )
            with self.assertRaisesRegex(ValueError, "R2 request failed") as caught:
                store.get("missing")
            self.assertNotIn("private-details", str(caught.exception))

    def test_cli_requires_explicit_manifest(self):
        result = subprocess.run(
            [os.sys.executable, str(pathlib.Path(publish.__file__)), "--dry-run"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("--manifest", result.stderr)

    def test_missing_historical_payload_refuses_all_writes(self):
        publish.publish(self.root, self.release(), self.store)
        publish.publish(self.root, self.release("2.0.0"), self.store)
        del self.store.objects["ChoscorDB-1.0.0.dmg"]
        self.store.actions.clear()
        with self.assertRaisesRegex(ValueError, "historical payload"):
            publish.publish(self.root, self.release("3.0.0"), self.store)
        self.assertEqual(self.store.actions, [])

    def test_historical_public_unavailability_blocks_all_writes(self):
        publish.publish(self.root, self.release(), self.store)
        self.store.actions.clear()
        self.store.unavailable = True
        with self.assertRaisesRegex(ValueError, "unavailable"):
            publish.publish(self.root, self.release("2.0.0"), self.store)
        self.assertEqual(self.store.actions, [])

    def test_conflicting_child_version_refuses_publication(self):
        manifest = self.release()
        feed = self.root / "choscordb-appcast.xml"
        feed.write_text(
            feed.read_text().replace(
                "<item>", "<item><sparkle:version>9.0.0</sparkle:version>"
            )
        )
        data = feed.read_bytes()
        entry = next(a for a in manifest["artifacts"] if a["role"] == "appcast")
        entry.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        with self.assertRaisesRegex(ValueError, "conflicting appcast"):
            publish.publish(self.root, manifest, self.store)
        self.assertEqual(self.store.actions, [])

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
                        "CFBundleShortVersionString": "6.8"
                        if framework == "QtCore"
                        else version,
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
if name == 'sign_update' and os.environ.get('FIXTURE_BAD_HISTORY') and any('history-verify-' in arg for arg in sys.argv): sys.exit(1)
if name == 'lipo': print('arm64')
if name == 'otool' and '-l' in sys.argv: print('Load command 0\n cmd LC_BUILD_VERSION\n platform 1\n minos 26.0\n sdk 26.5')
if name == 'hdiutil' and 'attach' in sys.argv:
    mount = pathlib.Path(sys.argv[sys.argv.index('-mountpoint') + 1])
    shutil.copytree(os.environ['FIXTURE_APP'], mount / 'ChoscorDB.app')
    (mount / 'Applications').symlink_to('/Applications')
    if os.environ.get('FIXTURE_BAD_MOUNT'):
        (mount / 'ChoscorDB.app/Contents/MacOS/choscordb').write_bytes(b'different app')
if name == 'codesign' and '-d' in sys.argv: print('Authority=Developer ID Application: Fixture\nCodeDirectory v=20500 size=800 flags=0x10000(runtime) hashes=10', file=sys.stderr)
if name == 'aws':
    if 'get-object' not in sys.argv: sys.exit(99)
    remote = os.environ.get('FIXTURE_REMOTE')
    key = sys.argv[sys.argv.index('--key') + 1]
    if remote and (pathlib.Path(remote) / key).exists():
        shutil.copyfile(pathlib.Path(remote) / key, sys.argv[-1])
        sys.exit(0)
    print('An error occurred (NoSuchKey)', file=sys.stderr)
    sys.exit(1)
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
            "aws",
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
            "--endpoint-url",
            "https://fixture.invalid",
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
        self.assertIn("choscor-downloads/ChoscorDB-1.0.0-manifest.json", result.stdout)
        self.assertIn("https://cdn.choscor.com/ChoscorDB-1.0.0.dmg", result.stdout)
        commands = log.read_text()
        self.assertIn("stapler validate", commands)
        self.assertIn("sign_update --account com.choscor.ChoscorDB --verify", commands)
        self.assertIn("get-object", commands)
        self.assertNotIn("put-object", commands)
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
        remote = self.root / "remote"
        remote.mkdir()
        (remote / "choscordb-appcast.xml").write_bytes(
            feed_path.read_bytes().replace(b"1.0.0", b"0.9.0")
        )
        (remote / "ChoscorDB-0.9.0.dmg").write_bytes(b"dmg-0.9.0")
        result = subprocess.run(
            command,
            env={**env, "FIXTURE_REMOTE": str(remote), "FIXTURE_BAD_HISTORY": "1"},
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("sign_update failed", result.stdout)
        self.assertNotIn("put-object", log.read_text())
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
