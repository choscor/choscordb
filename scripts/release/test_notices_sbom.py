import hashlib
import json
from pathlib import Path
import plistlib
import tempfile
import unittest

import notices_sbom as generator


class NoticesTest(unittest.TestCase):
    def test_sparkle_runtime_has_explicit_inventory_owner(self):
        base = self.stage / "choscordb.app/Contents"
        framework = base / "Frameworks/Sparkle.framework/Versions/B"
        framework.mkdir(parents=True)
        (framework / "Sparkle").write_bytes(b"sparkle runtime")
        (framework / "Resources").mkdir()
        (framework / "Resources/Info.plist").write_bytes(
            plistlib.dumps({"CFBundleShortVersionString": "2.9.6"})
        )
        notices = base / "Resources/licenses/Sparkle"
        notices.mkdir()
        (notices / "LICENSE").write_text("Sparkle MIT license")
        (notices / "source.json").write_text(
            json.dumps(
                {
                    "version": "2.9.6",
                    "sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
                    "url": "https://github.com/sparkle-project/Sparkle/releases/download/2.9.6/Sparkle-2.9.6.tar.xz",
                    "license": "MIT",
                }
            )
        )
        self.refresh_manifest()
        result = self.generate()
        doc = json.loads((result / "sbom.spdx.json").read_text())
        self.assertEqual(
            [p["versionInfo"] for p in doc["packages"] if p["name"] == "Sparkle"],
            ["2.9.6"],
        )

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "stage"
        self.stage.mkdir()
        payload = {
            "choscordb.app/Contents/MacOS/choscordb": b"application",
            "choscordb.app/Contents/Resources/licenses/LICENSE": b"application GPL",
            "choscordb.app/Contents/Resources/licenses/QScintilla/LICENSE": b"qscintilla GPL",
            "choscordb.app/Contents/Resources/licenses/QScintilla/source.json": json.dumps(
                {
                    "version": "2.14.1",
                    "license": "GPL-3.0-only",
                    "url": "https://example.org/qsci",
                }
            ).encode(),
            "choscordb.app/Contents/Frameworks/libqscintilla.dylib": b"qsci",
            "choscordb.app/Contents/Frameworks/QtCore.framework/Versions/A/QtCore": b"qt",
            "choscordb.app/Contents/Frameworks/QtCore.framework/Versions/A/Resources/Info.plist": plistlib.dumps(
                {"CFBundleShortVersionString": "6.8", "CFBundleVersion": "6.8.3"}
            ),
            "choscordb.app/Contents/PlugIns/platforms/libqcocoa.dylib": b"plugin",
        }
        repository = Path(__file__).resolve().parents[2]
        icons = repository / "desktop/resources/icons"
        for name in [
            "LICENSE-LUCIDE",
            "SOURCE-LUCIDE.json",
            "SOURCE-DATABASE-LOGOS.json",
        ]:
            payload["choscordb.app/Contents/Resources/licenses/" + name] = (
                icons / name
            ).read_bytes()
        for icon in [*icons.glob("*.svg"), *icons.glob("*.png")]:
            payload["choscordb.app/Contents/Resources/icons/" + icon.name] = (
                icon.read_bytes()
            )
        fonts = repository / "desktop/resources/fonts"
        for name in ["OFL.txt", "SOURCE-GEIST.json"]:
            payload["choscordb.app/Contents/Resources/licenses/" + name] = (
                fonts / name
            ).read_bytes()
        shadcn = repository / "docs/licenses/shadcn"
        for source_name, staged_name in [
            ("LICENSE.md", "LICENSE-SHADCN"),
            ("manifest.json", "SOURCE-SHADCN.json"),
        ]:
            payload["choscordb.app/Contents/Resources/licenses/" + staged_name] = (
                shadcn / source_name
            ).read_bytes()
        for name, data in payload.items():
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.source = self.root / "source.json"
        self.source.write_text(
            json.dumps(
                {
                    "source_kind": "working-tree snapshot",
                    "files": [
                        {
                            "path": path.relative_to(repository).as_posix(),
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                            "size": path.stat().st_size,
                        }
                        for path in sorted(
                            [*icons.iterdir(), *fonts.iterdir(), *shadcn.iterdir()]
                        )
                        if path.is_file()
                    ],
                }
            )
        )
        self.refresh_manifest()
        self.qt = self.root / "qt-notices"
        self.qt.mkdir()
        (self.qt / "source.json").write_text(
            json.dumps(
                {
                    "version": "6.8.3",
                    "license": "GPL-3.0-only",
                    "url": "https://example.org/qt",
                }
            )
        )
        (self.qt / "LICENSE").write_text("Qt license text")
        qt_record = json.loads((self.qt / "source.json").read_text())
        qt_record["modules"] = {
            "qtbase": {
                "version": "6.8.3",
                "sha256": generator.QT_SOURCE_HASHES["qtbase"],
                "copied_files": [
                    {
                        "path": "LICENSE",
                        "sha256": hashlib.sha256(b"Qt license text").hexdigest(),
                    }
                ],
            }
        }
        (self.qt / "source.json").write_text(json.dumps(qt_record))
        self.licenses = self.root / "cargo-licenses"
        (self.licenses / "dependency@1.0").mkdir(parents=True)
        (self.licenses / "dependency@1.0/LICENSE").write_text("MIT license text")
        self.metadata = self.root / "cargo.json"
        self.cargo = {
            "workspace_members": ["app"],
            "packages": [
                {
                    "id": "app",
                    "name": "app",
                    "version": "0.1",
                    "license": "GPL-3.0-or-later",
                },
                {"id": "dep", "name": "dependency", "version": "1.0", "license": "MIT"},
            ],
            "resolve": {
                "nodes": [
                    {
                        "id": "app",
                        "deps": [{"pkg": "dep", "dep_kinds": [{"kind": None}]}],
                    },
                    {"id": "dep", "deps": []},
                ]
            },
        }
        self.metadata.write_text(json.dumps(self.cargo))
        (self.licenses / "index.json").write_text(
            json.dumps(
                {
                    "root_package": "app",
                    "packages": [
                        {
                            "name": "dependency",
                            "version": "1.0",
                            "license": "MIT",
                            "source": None,
                            "files": [
                                {
                                    "path": "LICENSE",
                                    "sha256": hashlib.sha256(
                                        b"MIT license text"
                                    ).hexdigest(),
                                }
                            ],
                        }
                    ],
                }
            )
        )

    def refresh_manifest(self):
        files = [
            {
                "path": p.relative_to(self.stage).as_posix(),
                "size": p.stat().st_size,
                "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
            }
            for p in sorted(self.stage.rglob("*"))
            if p.is_file() and p.name not in {"manifest.json", "SHA256SUMS"}
        ]
        document = json.dumps(
            {
                "files": files,
                "source_candidate": {
                    "manifest_sha256": hashlib.sha256(
                        self.source.read_bytes()
                    ).hexdigest(),
                    "source_kind": "working-tree snapshot",
                    "git_revision": None,
                },
            }
        ).encode()
        (self.stage / "manifest.json").write_bytes(document)
        (self.stage / "SHA256SUMS").write_text(
            hashlib.sha256(document).hexdigest() + "  manifest.json\n"
        )

    def generate(self, name="output"):
        return generator.generate(
            self.stage,
            self.metadata,
            self.qt,
            "6.8.3",
            self.source,
            self.licenses,
            self.root / name,
            "candidate-fixture",
            "2026-09-12T00:00:00Z",
            "app",
            "aarch64-apple-darwin",
        )

    def test_shadcn_adaptations_have_verified_attribution(self):
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        shadcn = [p for p in document["packages"] if p["name"] == "shadcn/ui"]
        self.assertEqual(len(shadcn), 1)
        self.assertEqual(
            shadcn[0]["versionInfo"], "2b3e6d4f8d9161fe5c19340dc383aade392012dd"
        )
        self.assertEqual(shadcn[0]["licenseDeclared"], "MIT")
        self.assertIn("base-nova", shadcn[0]["sourceInfo"])
        license_path = (
            self.stage / "choscordb.app/Contents/Resources/licenses/LICENSE-SHADCN"
        )
        license_path.write_text("unreviewed replacement")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "shadcn"):
            self.generate("changed-shadcn-license")

    def test_embedded_geist_has_license_and_verified_source_hashes(self):
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        geist = [p for p in document["packages"] if p["name"] == "Geist"]
        self.assertEqual(len(geist), 1)
        self.assertEqual(
            geist[0]["versionInfo"], "10dc7658f13c38a474cde201bb09a4617267545b"
        )
        self.assertEqual(geist[0]["licenseDeclared"], "OFL-1.1")
        self.assertIn(
            "85a1c6b18a6b0a06dfe9fd4f6d6a5d4979f74ec861eaef4bc7868b5492b8a117",
            geist[0]["sourceInfo"],
        )
        source = json.loads(self.source.read_text())
        for entry in source["files"]:
            if entry["path"] == "desktop/resources/fonts/Geist-Bold.ttf":
                entry["sha256"] = "0" * 64
        self.source.write_text(json.dumps(source))
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "Geist"):
            self.generate("changed-font")

    def test_reviewed_icon_and_font_notices_reject_changed_payload_bytes(self):
        for relative, diagnostic in [
            ("icons/eye-off.svg", "Icon"),
            ("icons/postgresql.svg", "Icon"),
            ("icons/sqlite.svg", "Icon"),
            ("icons/mysql.png", "Icon"),
            ("licenses/SOURCE-DATABASE-LOGOS.json", "Database logo"),
            ("licenses/SOURCE-LUCIDE.json", "Lucide"),
            ("licenses/LICENSE-LUCIDE", "Lucide"),
            ("licenses/SOURCE-GEIST.json", "Geist"),
            ("licenses/OFL.txt", "Geist"),
            ("licenses/SOURCE-SHADCN.json", "shadcn"),
        ]:
            with self.subTest(relative=relative):
                path = self.stage / "choscordb.app/Contents/Resources" / relative
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                self.refresh_manifest()
                with self.assertRaisesRegex(ValueError, diagnostic):
                    self.generate()
                path.write_bytes(original)
                self.refresh_manifest()

    def test_database_logo_source_snapshot_must_match_reviewed_assets(self):
        original = self.source.read_bytes()
        for name in [
            "SOURCE-DATABASE-LOGOS.json",
            "postgresql.svg",
            "sqlite.svg",
            "mysql.png",
        ]:
            with self.subTest(name=name):
                source = json.loads(original)
                for entry in source["files"]:
                    if entry["path"] == "desktop/resources/icons/" + name:
                        entry["sha256"] = "0" * 64
                self.source.write_text(json.dumps(source))
                self.refresh_manifest()
                with self.assertRaisesRegex(ValueError, "Database logo|Icon"):
                    self.generate()
        self.source.write_bytes(original)

    def test_nested_duplicate_icon_cannot_hide_from_inventory(self):
        icons = self.stage / "choscordb.app/Contents/Resources/icons"
        (icons / "a").mkdir()
        (icons / "a/eye-off.svg").write_bytes((icons / "eye-off.svg").read_bytes())
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "icon inventory"):
            self.generate()

    def test_qt_full_patch_version_is_required_despite_shared_short_version(self):
        self.generate()
        info = (
            self.stage
            / "choscordb.app/Contents/Frameworks/QtCore.framework/Versions/A/Resources/Info.plist"
        )
        info.write_bytes(
            plistlib.dumps(
                {"CFBundleShortVersionString": "6.8", "CFBundleVersion": "6.8.4"}
            )
        )
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "Qt framework versions"):
            self.generate("wrong-qt-patch")
        info.write_bytes(plistlib.dumps({"CFBundleShortVersionString": "6.8"}))
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "Qt framework versions"):
            self.generate("unknown-qt-patch")

    def test_mysql_logo_notice_preserves_upstream_policy(self):
        notices = (self.generate() / "THIRD_PARTY_NOTICES.md").read_text()
        self.assertIn("MySQL is a trademark of Oracle", notices)
        self.assertIn("https://www.mysql.com/about/legal/logos.html", notices)

    def test_current_icon_inventory_uses_reviewed_assets(self):
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        packages = {p["SPDXID"]: p["name"] for p in document["packages"]}
        files = {f["SPDXID"]: f["fileName"] for f in document["files"]}
        icons = {
            files[r["relatedSpdxElement"]].split("/icons/")[1]: packages[
                r["spdxElementId"]
            ]
            for r in document["relationships"]
            if r["relationshipType"] == "CONTAINS"
            and "/icons/" in files[r["relatedSpdxElement"]]
        }
        self.assertEqual(len(icons), 30)
        self.assertEqual(icons["refresh-cw.svg"], "Lucide Icons")
        self.assertEqual(icons["commit.svg"], "ChoscorDB")
        self.assertEqual(icons["eye-off.svg"], "Lucide Icons")
        self.assertEqual(icons["code.svg"], "ChoscorDB")
        self.assertEqual(icons["app-mark.svg"], "ChoscorDB")
        self.assertEqual(icons["postgresql.svg"], "ChoscorDB")
        self.assertEqual(icons["sqlite.svg"], "ChoscorDB")
        self.assertEqual(icons["mysql.png"], "ChoscorDB")
        unknown = self.stage / "choscordb.app/Contents/Resources/icons/mystery.svg"
        unknown.write_bytes(b"unreviewed")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "icon"):
            self.generate("unknown-icon")

    def test_deterministic_spdx_ownership_licenses_and_checksums(self):
        first, second = self.generate(), self.generate("second")
        for p in first.rglob("*"):
            if p.is_file():
                self.assertEqual(
                    p.read_bytes(), (second / p.relative_to(first)).read_bytes()
                )
        document = json.loads((first / "sbom.spdx.json").read_text())
        self.assertEqual(document["spdxVersion"], "SPDX-2.3")
        self.assertEqual(document["dataLicense"], "CC0-1.0")
        self.assertEqual(document["SPDXID"], "SPDXRef-DOCUMENT")
        self.assertEqual(document["creationInfo"]["created"], "2026-09-12T00:00:00Z")
        self.assertTrue(document["creationInfo"]["creators"])
        self.assertTrue(document["documentNamespace"].startswith("urn:uuid:"))
        for package in document["packages"]:
            for field in [
                "name",
                "versionInfo",
                "downloadLocation",
                "licenseDeclared",
                "licenseConcluded",
                "copyrightText",
            ]:
                self.assertTrue(package[field])
            for name in (
                package["comment"].removeprefix("Included license texts: ").split(", ")
            ):
                self.assertFalse(Path(name).is_absolute())
                self.assertTrue((first / name).read_bytes())
        ids = [p["SPDXID"] for p in document["packages"] + document["files"]]
        self.assertEqual(len(ids), len(set(ids)))
        owned = [
            r["relatedSpdxElement"]
            for r in document["relationships"]
            if r["relationshipType"] == "CONTAINS"
        ]
        self.assertCountEqual(owned, [f["SPDXID"] for f in document["files"]])
        for f in document["files"]:
            self.assertTrue(f["licenseInfoInFiles"])
            self.assertTrue(f["licenseConcluded"])
            self.assertTrue(f["copyrightText"])
            path = self.stage / f["fileName"].removeprefix("./")
            self.assertEqual(
                f["checksums"][0]["checksumValue"],
                hashlib.sha256(path.read_bytes()).hexdigest(),
            )
        self.assertNotIn(str(self.root), (first / "sbom.spdx.json").read_text())
        for line in (first / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(
                digest, hashlib.sha256((first / name).read_bytes()).hexdigest()
            )

    def test_tamper_duplicate_ids_unknown_runtime_and_missing_license_fail(self):
        exe = self.stage / "choscordb.app/Contents/MacOS/choscordb"
        exe.write_bytes(b"tampered")
        with self.assertRaises(ValueError):
            self.generate()
        self.refresh_manifest()
        self.cargo["packages"].append(self.cargo["packages"][1])
        self.metadata.write_text(json.dumps(self.cargo))
        with self.assertRaises(ValueError):
            self.generate()
        self.cargo["packages"].pop()
        self.metadata.write_text(json.dumps(self.cargo))
        unknown = self.stage / "choscordb.app/Contents/Frameworks/libmystery.dylib"
        unknown.write_bytes(b"unknown")
        self.refresh_manifest()
        with self.assertRaises(ValueError):
            self.generate()
        unknown.unlink()
        self.refresh_manifest()
        (self.licenses / "dependency@1.0/LICENSE").unlink()
        with self.assertRaises(ValueError):
            self.generate()

    def test_only_normal_reachable_dependencies_need_licenses(self):
        for name, kind in [
            ("devonly", "dev"),
            ("buildonly", "build"),
            ("unreachable", None),
        ]:
            self.cargo["packages"].append(
                {"id": name, "name": name, "version": "1", "license": "MIT"}
            )
            self.cargo["resolve"]["nodes"].append({"id": name, "deps": []})
            if name != "unreachable":
                self.cargo["resolve"]["nodes"][0]["deps"].append(
                    {"pkg": name, "dep_kinds": [{"kind": kind}]}
                )
        self.metadata.write_text(json.dumps(self.cargo))
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        self.assertEqual(
            {p["name"] for p in document["packages"]},
            {
                "ChoscorDB",
                "Qt",
                "QScintilla",
                "Lucide Icons",
                "Geist",
                "shadcn/ui",
                "dependency",
            },
        )
        lucide = next(p for p in document["packages"] if p["name"] == "Lucide Icons")
        self.assertEqual(lucide["versionInfo"], "1.27.0")
        self.assertEqual(lucide["licenseDeclared"], "ISC AND MIT")
        self.assertIn("4aec3f892fd6c23063bc2fead83c899b5d412b1c", lucide["sourceInfo"])
        self.cargo["packages"][0]["name"] = "different-root"
        self.metadata.write_text(json.dumps(self.cargo))
        with self.assertRaisesRegex(ValueError, "root"):
            self.generate("bad-root")

    def test_version_source_relationship_and_existing_output_fail_closed(self):
        record = json.loads((self.qt / "source.json").read_text())
        record["version"] = "6.9.0"
        (self.qt / "source.json").write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, "version"):
            self.generate()
        record["version"] = "6.8.3"
        (self.qt / "source.json").write_text(json.dumps(record))
        original = self.source.read_bytes()
        self.source.write_bytes(original + b" ")
        with self.assertRaisesRegex(ValueError, "relationship"):
            self.generate()
        self.source.write_bytes(original)
        self.generate()
        with self.assertRaises(FileExistsError):
            self.generate()

    def test_unknown_plugin_and_proc_macro_relationship(self):
        plugin = (
            self.stage / "choscordb.app/Contents/PlugIns/imageformats/libunknown.dylib"
        )
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"unknown")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "plugin"):
            self.generate()
        plugin.unlink()
        self.refresh_manifest()
        self.cargo["packages"][1]["targets"] = [{"kind": ["proc-macro"]}]
        self.metadata.write_text(json.dumps(self.cargo))
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        dependency = next(
            p["SPDXID"] for p in document["packages"] if p["name"] == "dependency"
        )
        self.assertFalse(
            any(
                r["relationshipType"] == "STATIC_LINK"
                and r["relatedSpdxElement"] == dependency
                for r in document["relationships"]
            )
        )
        self.assertTrue(
            any(
                r["relationshipType"] == "BUILD_DEPENDENCY_OF"
                and r["spdxElementId"] == dependency
                for r in document["relationships"]
            )
        )

    def test_license_text_tampering_is_rejected(self):
        (self.licenses / "dependency@1.0/LICENSE").write_text("changed license")
        with self.assertRaises(ValueError):
            self.generate()

    def test_bundled_sqlite_native_identity_and_license(self):
        package = self.root / "sqlite-package"
        (package / "sqlite3").mkdir(parents=True)
        (package / "Cargo.toml").write_text("fixture")
        defines = (
            '#define SQLITE_VERSION "3.50.2"\n#define SQLITE_SOURCE_ID "2025-06-28 14:00:48 '
            + "a" * 64
            + '"\n'
        )
        header = package / "sqlite3/sqlite3.h"
        header.write_text(defines)
        c = package / "sqlite3/sqlite3.c"
        c.write_text(
            "/* The author disclaims copyright to this source code.\n** May you do good and not evil.\n** May you find forgiveness for yourself and forgive others.\n** May you share freely, never taking more than you give. */\n"
            + defines
        )
        self.cargo["packages"].append(
            {
                "id": "sqlite",
                "name": "libsqlite3-sys",
                "version": "0.35.0",
                "license": "MIT",
                "manifest_path": str(package / "Cargo.toml"),
            }
        )
        self.cargo["resolve"]["nodes"][0]["deps"].append(
            {"pkg": "sqlite", "dep_kinds": [{"kind": None}]}
        )
        self.cargo["resolve"]["nodes"].append(
            {"id": "sqlite", "features": ["bundled"], "deps": []}
        )
        self.metadata.write_text(json.dumps(self.cargo))
        folder = self.licenses / "libsqlite3-sys@0.35.0"
        folder.mkdir()
        (folder / "LICENSE").write_bytes(b"MIT wrapper license")
        index = json.loads((self.licenses / "index.json").read_text())
        index["packages"].append(
            {
                "name": "libsqlite3-sys",
                "version": "0.35.0",
                "license": "MIT",
                "source": None,
                "files": [
                    {
                        "path": "LICENSE",
                        "sha256": hashlib.sha256(b"MIT wrapper license").hexdigest(),
                    }
                ],
            }
        )
        (self.licenses / "index.json").write_text(json.dumps(index))
        document = json.loads((self.generate() / "sbom.spdx.json").read_text())
        native = next(p for p in document["packages"] if p["name"] == "SQLite")
        self.assertEqual(native["versionInfo"], "3.50.2")
        self.assertEqual(native["licenseDeclared"], "blessing")
        self.assertIn(hashlib.sha256(c.read_bytes()).hexdigest(), native["sourceInfo"])
        header.write_text(defines.replace("3.50.2", "3.49.0"))
        with self.assertRaisesRegex(ValueError, "mismatch"):
            self.generate("bad-sqlite")

    def test_svg_requires_verified_module_and_qt_copies_match_hashes(self):
        plugin = (
            self.stage / "choscordb.app/Contents/PlugIns/iconengines/libqsvgicon.dylib"
        )
        plugin.parent.mkdir(parents=True)
        plugin.write_bytes(b"svg")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "module"):
            self.generate()
        (self.qt / "qtsvg").mkdir()
        (self.qt / "qtsvg/LICENSE").write_bytes(b"svg license")
        record = json.loads((self.qt / "source.json").read_text())
        record["modules"]["qtsvg"] = {
            "version": "6.8.3",
            "sha256": generator.QT_SOURCE_HASHES["qtsvg"],
            "copied_files": [
                {
                    "path": "qtsvg/LICENSE",
                    "sha256": hashlib.sha256(b"svg license").hexdigest(),
                }
            ],
        }
        (self.qt / "source.json").write_text(json.dumps(record))
        self.generate()
        (self.qt / "LICENSE").write_text("changed Qt license")
        with self.assertRaisesRegex(ValueError, "hashes"):
            self.generate("tampered-qt")


if __name__ == "__main__":
    unittest.main()
