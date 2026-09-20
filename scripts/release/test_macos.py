"""Production release CLI rejection tests use real disposable Git repositories."""

import json
import base64
import hashlib
import platform
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import tarfile

CLI = Path(__file__).with_name("macos.py")


class ReleaseCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        (self.root / "Cargo.toml").write_text(
            '[workspace.package]\nversion = "1.2.3"\n'
        )
        (self.root / "CHANGELOG.md").write_text("# Changes\n\n## 1.2.3\nRelease.\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git(
            "-c",
            "user.name=Fixture",
            "-c",
            "user.email=fixture@example.invalid",
            "commit",
            "-qm",
            "fixture",
        )

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.root), *args],
            check=True,
            capture_output=True,
            text=True,
        ).stdout

    def cli(self, *args):
        return subprocess.run(
            [sys.executable, str(CLI), *map(str, args)],
            capture_output=True,
            text=True,
            env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
        )

    def test_preflight_reports_authoritative_version_and_commit(self):
        result = self.cli("preflight", "--root", self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["version"], "1.2.3")
        self.assertEqual(report["source_commit"], self.git("rev-parse", "HEAD").strip())
        self.assertEqual(self.git("status", "--porcelain"), "")

    def test_preflight_rejects_untracked_file(self):
        (self.root / "forgotten.txt").write_text("new source")
        result = self.cli("preflight", "--root", self.root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean tracked and untracked", result.stderr)

    def test_version_override_must_match(self):
        result = self.cli("preflight", "--root", self.root, "--version", "1.2.4")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("authoritative", result.stderr)

    def test_changelog_required(self):
        (self.root / "CHANGELOG.md").write_text("# No matching release\n")
        self.git("add", ".")
        self.git(
            "-c",
            "user.name=Fixture",
            "-c",
            "user.email=f@example.invalid",
            "commit",
            "-qm",
            "no notes",
        )
        result = self.cli("preflight", "--root", self.root)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CHANGELOG", result.stderr)


@unittest.skipUnless(
    platform.system() == "Darwin" and platform.machine() == "arm64",
    "Production host contract requires Apple Silicon macOS",
)
class PackageFailureCLI(ReleaseCLI):
    def test_external_failures_never_produce_verified_output_or_publish(self):
        # Actual clean Git/source/archive/hash path; only external tools are fixtures.
        import source_archive

        for name in source_archive.ROOT_FILES:
            path = self.root / name
            if not path.exists():
                path.write_text("fixture\n")
        for name in source_archive.SOURCE_ROOTS:
            (self.root / name).mkdir(exist_ok=True)
            (self.root / name / "fixture.txt").write_text("tracked source")
        (self.root / ".gitignore").write_text("build/\n")
        self.git("add", ".")
        self.git(
            "-c",
            "user.name=Fixture",
            "-c",
            "user.email=f@example.invalid",
            "commit",
            "-qm",
            "source fixtures",
        )
        before = self.git("rev-parse", "HEAD")
        tools = Path(self.temp.name) / "tools"
        tools.mkdir()
        log = Path(self.temp.name) / "tool.log"
        script = """#!{python}
import os,sys,pathlib
name=pathlib.Path(sys.argv[0]).name
with open(os.environ['RELEASE_FIXTURE_LOG'],'a') as log: log.write(name+' '+' '.join(sys.argv[1:])+'\\n')
if name=='xcrun' and '--show-sdk-version' in sys.argv: print('26.5')
if name=='xcrun' and 'history' in sys.argv and os.environ.get('RELEASE_FAILURE')=='profile': sys.exit(7)
if name=='security': print('1) '+ 'A'*40 +' "Developer ID Application: Fixture"')
if name=='generate_keys':
    if os.environ.get('RELEASE_FAILURE')=='key': sys.exit(8)
    print({public!r})
if name=='cmake':
    print('intentional configure failure',file=sys.stderr)
    sys.exit(19)
""".format(python=sys.executable, public=base64.b64encode(bytes(range(32))).decode())
        for name in [
            "cmake",
            "ninja",
            "cargo",
            "rustup",
            "xcrun",
            "codesign",
            "security",
            "hdiutil",
            "ditto",
            "lipo",
            "otool",
            "spctl",
        ]:
            path = tools / name
            path.write_text(script)
            path.chmod(0o755)
        # host_tools only checks converter importability before CMake invokes it.
        (tools / "cairosvg.py").write_text("")
        deps = self.root / "build/dependencies"
        for name in ["qt/6.8.3/macos", "qscintilla", "sparkle/bin"]:
            (deps / name).mkdir(parents=True)
        keytool = deps / "sparkle/bin/generate_keys"
        keytool.write_text(script)
        keytool.chmod(0o755)
        (deps / "dependencies.json").write_text(
            json.dumps(
                {
                    "Qt": "6.8.3",
                    "QScintilla": "2.14.1",
                    "Sparkle": "2.9.6",
                    "Sparkle_sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
                    "minimum_macos": "26.0",
                    "architecture": "arm64",
                }
            )
        )
        (deps / "payload-hashes.json").write_text(
            json.dumps(
                {
                    "sparkle/bin/generate_keys": hashlib.sha256(
                        keytool.read_bytes()
                    ).hexdigest()
                }
            )
        )
        for failure in ["key", "profile", "configure"]:
            with self.subTest(failure=failure):
                log.write_text("")
                output = self.root / "build" / failure
                result = subprocess.run(
                    [
                        sys.executable,
                        str(CLI),
                        "package",
                        "--root",
                        str(self.root),
                        "--dependencies",
                        str(deps),
                        "--output",
                        str(output),
                    ],
                    capture_output=True,
                    text=True,
                    env={
                        **os.environ,
                        "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                        "PYTHONPATH": str(tools),
                        "PYTHONDONTWRITEBYTECODE": "1",
                        "RELEASE_FIXTURE_LOG": str(log),
                        "RELEASE_FAILURE": failure,
                    },
                )
                self.assertNotEqual(result.returncode, 0, result.stdout)
                calls = log.read_text()
                self.assertNotIn("aws ", calls)
                self.assertNotIn("upload", calls)
                self.assertFalse((output / "ChoscorDB-1.2.3-manifest.json").exists())
                if failure == "configure":
                    self.assertIn("cmake -S", calls)
                    self.assertTrue((output / "INCOMPLETE.json").is_file())
                    self.assertTrue(
                        (output / "source/choscordb-source.tar.gz").is_file()
                    )
                    self.assertIn(
                        "intentional configure failure",
                        (output / "logs/failures.log").read_text(),
                    )
                else:
                    self.assertNotIn("cmake -S", calls)
                    self.assertFalse(output.exists())
                self.assertEqual(self.git("status", "--porcelain"), "")
                self.assertEqual(self.git("rev-parse", "HEAD"), before)
                self.assertEqual(self.git("tag"), "")


class ManifestCLI(unittest.TestCase):
    def test_public_manifest_refuses_private_tool_paths_before_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(
                json.dumps(
                    {
                        "format_version": 1,
                        "production": True,
                        "status": "verified",
                        "sparkle_tools": "/Users/private/dependencies/sparkle",
                    }
                )
            )
            result = subprocess.run(
                [sys.executable, str(CLI), "verify", "--manifest", str(path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("local tool paths", result.stderr)
            self.assertNotIn("/Users/private", result.stderr)

    def test_public_manifest_export_rejects_private_metadata(self):
        import macos

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            with self.assertRaisesRegex(ValueError, "local tool paths"):
                macos.write_public_manifest(
                    path, {"sparkle_tools": "/Users/private/sparkle"}
                )
            self.assertFalse(path.exists())
            public = {
                "version": "1.2.3",
                "app_path": "staged/ChoscorDB.app",
                "sparkle_public_key": "public-only",
            }
            macos.write_public_manifest(path, public)
            self.assertEqual(json.loads(path.read_text()), public)
            self.assertNotIn(directory, path.read_text())

    def test_changed_artifact_rejected_before_signing_tools(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "ChoscorDB-1.2.3.dmg").write_bytes(b"tampered")
            manifest = {
                "format_version": 1,
                "production": True,
                "status": "verified",
                "version": "1.2.3",
                "source_commit": "a" * 40,
                "base_url": "https://cdn.choscor.com",
                "feed_url": "https://cdn.choscor.com/choscordb-appcast.xml",
                "artifacts": [
                    {
                        "path": "ChoscorDB-1.2.3.dmg",
                        "role": "dmg",
                        "sha256": "0" * 64,
                        "size": 8,
                    }
                ],
            }
            (root / "manifest.json").write_text(json.dumps(manifest))
            result = subprocess.run(
                [
                    sys.executable,
                    str(CLI),
                    "verify",
                    "--manifest",
                    str(root / "manifest.json"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("artifact changed", result.stderr)

    def test_manifest_requires_canonical_metadata_not_any_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = []
            import hashlib

            for name, role in [
                ("ChoscorDB-1.2.3.dmg", "dmg"),
                ("ChoscorDB.dmg", "latest"),
                ("choscordb-appcast.xml", "appcast"),
                ("arbitrary.tar.gz", "source"),
                ("arbitrary.txt", "metadata"),
            ]:
                (root / name).write_bytes(b"fixture")
                artifacts.append(
                    {
                        "path": name,
                        "role": role,
                        "size": 7,
                        "sha256": hashlib.sha256(b"fixture").hexdigest(),
                    }
                )
            manifest = {
                "format_version": 1,
                "production": True,
                "status": "verified",
                "version": "1.2.3",
                "source_commit": "a" * 40,
                "base_url": "https://cdn.choscor.com",
                "feed_url": "https://cdn.choscor.com/choscordb-appcast.xml",
                "artifacts": artifacts,
            }
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest))
            result = subprocess.run(
                [sys.executable, str(CLI), "verify", "--manifest", str(path)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Required canonical artifact", result.stderr)

    def test_incomplete_manifest_never_qualifies(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(
                json.dumps(
                    {"format_version": 1, "production": True, "status": "building"}
                )
            )
            result = subprocess.run(
                [sys.executable, str(CLI), "verify", "--manifest", str(path)],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("not a verified production release", result.stderr)


@unittest.skipUnless(
    platform.system() == "Darwin" and platform.machine() == "arm64",
    "Production host contract requires Apple Silicon macOS",
)
class PrepareCLI(unittest.TestCase):
    def test_qt_downloads_only_release_modules_into_reusable_isolated_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tools = root / "tools"
            tools.mkdir()
            for name in [
                "cmake",
                "ninja",
                "cargo",
                "rustup",
                "xcrun",
                "codesign",
                "security",
                "hdiutil",
                "ditto",
                "lipo",
                "otool",
                "spctl",
            ]:
                tool = tools / name
                tool.write_text("#!" + sys.executable + '\nprint("26.5")\n')
                tool.chmod(0o755)
            (tools / "cairosvg.py").write_text("")
            distribution = tools / "aqtinstall-3.3.0.dist-info"
            distribution.mkdir()
            (distribution / "METADATA").write_text(
                "Metadata-Version: 2.1\nName: aqtinstall\nVersion: 3.3.0\n"
            )
            log = root / "aqt.json"
            (tools / "aqt.py").write_text(
                'import os,sys,json,pathlib\npathlib.Path(os.environ["FIXTURE_AQT_LOG"]).write_text(json.dumps({"arguments":sys.argv[1:],"cwd":os.getcwd()}))\nsys.exit(42)\n'
            )
            deps = root / "dependencies"
            result = subprocess.run(
                [
                    sys.executable,
                    str(CLI),
                    "prepare",
                    "--dependencies",
                    str(deps),
                    "--qt-mirror",
                    "https://ftp.fau.de/qtproject",
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PYTHONPATH": str(tools),
                    "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                    "FIXTURE_AQT_LOG": str(log),
                },
            )
            self.assertNotEqual(result.returncode, 0)
            report = json.loads(log.read_text())
            self.assertEqual(Path(report["cwd"]).resolve(), deps.resolve())
            args = report["arguments"]
            self.assertIn("--keep", args)
            self.assertEqual(
                args[args.index("--base") + 1], "https://ftp.fau.de/qtproject"
            )
            self.assertEqual(
                args[args.index("--archive-dest") + 1],
                str((deps / "cache/qt").resolve()),
            )
            self.assertEqual(
                args[args.index("--archives") + 1 :], ["qtbase", "qttools", "qtsvg"]
            )
            self.assertFalse((deps / "dependencies.json").exists())
            qt = deps / "qt/6.8.3/macos"
            (qt / "bin").mkdir(parents=True)
            (qt / "bin/qmake").write_text("#!" + sys.executable + "\nprint('6.8.3')\n")
            (qt / "bin/qmake").chmod(0o755)
            bootstrap = root / "scripts/ci/bootstrap_qscintilla.py"
            bootstrap.parent.mkdir(parents=True)
            bootstrap.write_text(
                "import sys\nprint('native build reached',file=sys.stderr)\nsys.exit(99)\n"
            )
            (qt / "lib").mkdir()
            library = qt / "lib/QtCore"
            library.write_bytes(b"verified Qt")
            (deps / "payload-hashes.json").write_text(
                json.dumps(
                    {
                        p.relative_to(deps).as_posix(): hashlib.sha256(
                            p.read_bytes()
                        ).hexdigest()
                        for p in qt.rglob("*")
                        if p.is_file()
                    }
                )
            )
            library.write_bytes(b"tampered Qt")
            result = subprocess.run(
                [
                    sys.executable,
                    str(CLI),
                    "prepare",
                    "--root",
                    str(root),
                    "--dependencies",
                    str(deps),
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PYTHONPATH": str(tools),
                    "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                    "FIXTURE_AQT_LOG": str(log),
                },
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Prepared Qt cache has changed", result.stderr)
            self.assertFalse((deps / "dependencies.json").exists())
            library.write_bytes(b"verified Qt")
            log.unlink()
            result = subprocess.run(
                [
                    sys.executable,
                    str(CLI),
                    "prepare",
                    "--root",
                    str(root),
                    "--dependencies",
                    str(deps),
                ],
                capture_output=True,
                text=True,
                env={
                    **os.environ,
                    "PYTHONPATH": str(tools),
                    "PATH": str(tools) + os.pathsep + os.environ["PATH"],
                    "FIXTURE_AQT_LOG": str(log),
                },
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("native build reached", result.stderr)
            self.assertFalse(
                log.exists(), "Untouched verified Qt must reuse the installed prefix"
            )


class RuntimePayload(unittest.TestCase):
    def test_strips_headers_modules_and_symbols_but_keeps_runtime_helpers(self):
        import macos

        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "ChoscorDB.app"
            framework = app / "Contents/Frameworks/Sparkle.framework"
            for name in [
                "Versions/B/Headers/Sparkle.h",
                "Versions/B/Modules/module.modulemap",
                "Versions/B/PrivateHeaders/private.h",
                "Versions/B/Updater.app/Contents/MacOS/Updater",
                "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer",
                "Versions/B/Sparkle",
                "Versions/B/Resources/en.lproj/strings",
                "Symbols.dSYM/Contents/Resources/DWARF/Sparkle",
            ]:
                path = framework / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            (framework / "Headers").symlink_to("Versions/B/Headers")
            macos.strip_development_payload(app)
            self.assertFalse((framework / "Headers").is_symlink())
            self.assertFalse((framework / "Versions/B/Headers").exists())
            self.assertFalse((framework / "Versions/B/PrivateHeaders").exists())
            self.assertFalse((framework / "Versions/B/Modules").exists())
            self.assertFalse((framework / "Symbols.dSYM").exists())
            self.assertEqual(
                (
                    framework / "Versions/B/Updater.app/Contents/MacOS/Updater"
                ).read_bytes(),
                b"fixture",
            )
            self.assertEqual(
                (
                    framework
                    / "Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer"
                ).read_bytes(),
                b"fixture",
            )
            self.assertEqual(
                (framework / "Versions/B/Resources/en.lproj/strings").read_bytes(),
                b"fixture",
            )


class SignatureAssessment(unittest.TestCase):
    def test_runtime_path_does_not_prove_hardened_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ["codesign", "xcrun", "spctl"]:
                tool = root / name
                tool.write_text(
                    "#!"
                    + sys.executable
                    + '\nimport os,sys\nif "-d" in sys.argv:\n print("Executable=/tmp/runtime/ChoscorDB.app/Contents/MacOS/choscordb\\nAuthority=Developer ID Application: Fixture\\nCodeDirectory v=20500 size=800 flags="+os.environ["FIXTURE_FLAGS"]+" hashes=10",file=sys.stderr)\n'
                )
                tool.chmod(0o755)
            for flags, success in [("0x0(none)", False), ("0x10000(runtime)", True)]:
                with self.subTest(flags=flags):
                    result = subprocess.run(
                        [
                            sys.executable,
                            "-c",
                            'import macos; from pathlib import Path; macos.assess(Path("/tmp/runtime/ChoscorDB.app"),Path("fixture.dmg"))',
                        ],
                        capture_output=True,
                        text=True,
                        env={
                            **os.environ,
                            "PYTHONPATH": str(CLI.parent),
                            "PATH": str(root) + os.pathsep + os.environ["PATH"],
                            "FIXTURE_FLAGS": flags,
                        },
                    )
                    self.assertEqual(result.returncode == 0, success, result.stderr)


class PortableArchive(unittest.TestCase):
    def test_archive_removes_owner_metadata_and_preserves_payload(self):
        import macos

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            vendor = root / "vendor"
            source = vendor / "crate with spaces" / "src"
            source.mkdir(parents=True)
            source.chmod(0o755)
            binary = source / "tool"
            binary.write_bytes(b"#!/bin/sh\nprintf fixture\n")
            binary.chmod(0o755)
            long_source = source / ("x" * 120 + ".rs")
            long_source.write_bytes(b"long name fixture")
            config = root / "vendor-config.toml"
            config.write_bytes(b'[source.vendored-sources]\ndirectory = "vendor"\n')
            config.chmod(0o644)
            first = root / "first.tar.gz"
            second = root / "second.tar.gz"
            sources = [(vendor, "vendor"), (config, ".cargo/config.toml")]
            macos.write_portable_tar_gz(first, sources)
            with tarfile.open(first) as archive:
                members = archive.getmembers()
                self.assertTrue(any(member.isdir() for member in members))
                self.assertTrue(any("path" in member.pax_headers for member in members))
                for member in members:
                    self.assertEqual(
                        (member.uid, member.gid, member.uname, member.gname),
                        (0, 0, "", ""),
                        member.name,
                    )
                    self.assertEqual(member.mtime, 0)
                    self.assertFalse(
                        {"uid", "gid", "uname", "gname", "atime", "ctime", "mtime"}
                        & member.pax_headers.keys()
                    )
                    self.assertNotIn(str(root), str(member.pax_headers))
                tool = archive.getmember("vendor/crate with spaces/src/tool")
                self.assertEqual(tool.mode, 0o755)
                self.assertEqual(archive.extractfile(tool).read(), binary.read_bytes())
                settings = archive.getmember(".cargo/config.toml")
                self.assertEqual(settings.mode, 0o644)
                self.assertEqual(
                    archive.extractfile(settings).read(), config.read_bytes()
                )
            os.utime(binary, (123456789, 123456789))
            os.utime(config, (234567890, 234567890))
            macos.write_portable_tar_gz(second, sources)
            self.assertEqual(first.read_bytes(), second.read_bytes())


if __name__ == "__main__":
    unittest.main()
