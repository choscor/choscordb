import hashlib
import io
import json
from pathlib import Path
import plistlib
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import release_consistency

from release_consistency import verify_release_consistency


class ConsistencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        # Hash DATA only is substituted for tiny fixture archives; validation logic
        # and production pins remain unchanged outside this test's lifetime.
        fixture_bytes = b"controlled native fixture"
        fixture_hash = hashlib.sha256(fixture_bytes).hexdigest()
        pins = {name: fixture_hash for name in release_consistency.NATIVE_SOURCE_HASHES}
        patcher = patch.dict(release_consistency.NATIVE_SOURCE_HASHES, pins, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        for name in pins:
            (self.root / ("ChoscorDB-1.2.3-" + name)).write_bytes(fixture_bytes)
        self.manifest = {
            "version": "1.2.3",
            "source_commit": "a" * 40,
            "app_path": "staged/ChoscorDB.app",
            "dependencies": {
                "Qt": "6.8.3",
                "QScintilla": "2.14.1",
                "Sparkle": "2.9.6",
                "Sparkle_sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
                "minimum_macos": "26.0",
                "architecture": "arm64",
            },
            "artifacts": [],
        }
        app = self.root / self.manifest["app_path"]
        for framework, version in [("QtCore", "6.8.3"), ("Sparkle", "2.9.6")]:
            path = (
                app / f"Contents/Frameworks/{framework}.framework/Resources/Info.plist"
            )
            path.parent.mkdir(parents=True)
            path.write_bytes(
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
                    "sha256": self.manifest["dependencies"]["Sparkle_sha256"],
                },
            ),
        ]:
            path = app / f"Contents/Resources/licenses/{dependency}/source.json"
            path.parent.mkdir(parents=True)
            path.write_text(json.dumps(record))
        self.files = {
            "Cargo.toml": b'[workspace.package]\nversion = "1.2.3"\n',
            "CHANGELOG.md": b"# Changes\n\n## 1.2.3\nRelease\n",
        }
        self.write_source()

    def write_source(self, files=None, archive_files=None):
        files = self.files if files is None else files
        archive_files = files if archive_files is None else archive_files
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
        (self.root / "ChoscorDB-1.2.3-source.json").write_text(json.dumps(source))
        with tarfile.open(
            self.root / "ChoscorDB-1.2.3-source.tar.gz", "w:gz"
        ) as archive:
            for name, data in archive_files.items():
                member = tarfile.TarInfo("choscordb-source/" + name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        self.refresh_checksums()

    def refresh_checksums(self):
        self.manifest["artifacts"] = [
            {"path": p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
            for p in sorted(self.root.glob("ChoscorDB-*"))
            if not p.name.endswith("SHA256SUMS")
        ]
        sums = self.root / "ChoscorDB-1.2.3-SHA256SUMS"
        sums.write_text(
            "".join(f"{a['sha256']}  {a['path']}\n" for a in self.manifest["artifacts"])
        )
        self.manifest["artifacts"].append(
            {"path": sums.name, "sha256": hashlib.sha256(sums.read_bytes()).hexdigest()}
        )

    def test_consistent_release_is_accepted(self):
        verify_release_consistency(self.root, self.manifest)

    def test_source_commit_must_agree(self):
        self.manifest["source_commit"] = "b" * 40
        with self.assertRaisesRegex(ValueError, "source commit"):
            verify_release_consistency(self.root, self.manifest)

    def test_archive_content_must_match_provenance(self):
        self.write_source(archive_files={**self.files, "Cargo.toml": b"wrong source"})
        with self.assertRaisesRegex(ValueError, "source archive"):
            verify_release_consistency(self.root, self.manifest)

    def test_authoritative_archive_version_must_agree(self):
        self.write_source(
            files={
                **self.files,
                "Cargo.toml": b'[workspace.package]\nversion = "9.0.0"\n',
            }
        )
        with self.assertRaisesRegex(ValueError, "source version"):
            verify_release_consistency(self.root, self.manifest)

    def test_checksum_contents_must_agree(self):
        (self.root / "ChoscorDB-1.2.3-SHA256SUMS").write_text(
            "0" * 64 + "  ChoscorDB-1.2.3-source.json\n"
        )
        with self.assertRaisesRegex(ValueError, "checksum inventory"):
            verify_release_consistency(self.root, self.manifest)

    def test_dependency_record_must_agree(self):
        self.manifest["dependencies"]["Qt"] = "99.0.0"
        with self.assertRaisesRegex(ValueError, "dependency pins"):
            verify_release_consistency(self.root, self.manifest)

    def test_bundled_framework_must_agree(self):
        path = (
            self.root
            / self.manifest["app_path"]
            / "Contents/Frameworks/QtCore.framework/Resources/Info.plist"
        )
        path.write_bytes(
            plistlib.dumps(
                {"CFBundleShortVersionString": "6.8", "CFBundleVersion": "6.8.4"}
            )
        )
        with self.assertRaisesRegex(ValueError, "Qt framework"):
            verify_release_consistency(self.root, self.manifest)

    def test_archive_unsafe_member_rejected_without_extracting(self):
        self.write_source(archive_files={"../escape": b"unsafe"})
        with self.assertRaisesRegex(ValueError, "source archive"):
            verify_release_consistency(self.root, self.manifest)

    def test_duplicate_and_symlink_source_members_are_rejected(self):
        for kind in ["duplicate", "symlink"]:
            with self.subTest(kind=kind):
                self.write_source()
                with tarfile.open(
                    self.root / "ChoscorDB-1.2.3-source.tar.gz", "w:gz"
                ) as archive:
                    for name, data in self.files.items():
                        member = tarfile.TarInfo("choscordb-source/" + name)
                        member.size = len(data)
                        archive.addfile(member, io.BytesIO(data))
                    extra = tarfile.TarInfo("choscordb-source/Cargo.toml")
                    if kind == "symlink":
                        extra.type = tarfile.SYMTYPE
                        extra.linkname = "/tmp/outside-source"
                    archive.addfile(extra)
                self.refresh_checksums()
                with self.assertRaisesRegex(ValueError, "source archive"):
                    verify_release_consistency(self.root, self.manifest)

    def test_source_changelog_must_contain_release_version(self):
        self.write_source(
            files={**self.files, "CHANGELOG.md": b"# Changes\n\n## 9.0.0\n"}
        )
        with self.assertRaisesRegex(ValueError, "source changelog"):
            verify_release_consistency(self.root, self.manifest)

    def test_coherent_wrong_native_source_is_rejected(self):
        native = self.root / "ChoscorDB-1.2.3-qtbase-6.8.3.tar.xz"
        native.write_bytes(b"wrong native source")
        self.refresh_checksums()
        with self.assertRaisesRegex(ValueError, "native source"):
            verify_release_consistency(self.root, self.manifest)


class ProductionPinsTests(unittest.TestCase):
    def test_native_source_pins_are_reviewed_literal_values(self):
        self.assertEqual(
            release_consistency.NATIVE_SOURCE_HASHES,
            {
                "qtbase-6.8.3.tar.xz": "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80",
                "qtsvg-6.8.3.tar.xz": "35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438",
                "QScintilla_src-2.14.1.tar.gz": "dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d",
            },
        )


if __name__ == "__main__":
    unittest.main()
