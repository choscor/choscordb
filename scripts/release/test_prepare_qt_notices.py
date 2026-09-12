import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import prepare_qt_notices as notices


class QtNoticesTest(unittest.TestCase):
    def fixture(self, directory, extra=None):
        archive = Path(directory) / "qt.tar.xz"
        files = {
            f"{notices.ROOT}/LICENSES/LGPL-3.0-only.txt": b"license",
            f"{notices.ROOT}/REUSE.toml": b"reuse",
            f"{notices.ROOT}/.reuse/dep5": b"dep5",
            f"{notices.ROOT}/src/3rdparty/demo/qt_attribution.json": b"{}",
        }
        files.update(extra or {})
        with tarfile.open(archive, "w:xz") as target:
            for name, data in files.items():
                member = tarfile.TarInfo(name)
                member.size = len(data)
                target.addfile(member, io.BytesIO(data))
        return archive

    def test_verified_source_metadata_and_notices_are_prepared(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.fixture(directory)
            output = Path(directory) / "notices"
            with patch.object(notices, "SHA256", hashlib.sha256(archive.read_bytes()).hexdigest()):
                notices.prepare(archive, output)
            record = json.loads((output / "source.json").read_text())
            self.assertEqual(record["version"], notices.VERSION)
            self.assertTrue((output / "LICENSES/LGPL-3.0-only.txt").is_file())
            self.assertTrue((output / "reuse/dep5").is_file())
            self.assertTrue((output / "attributions/src/3rdparty/demo/qt_attribution.json").is_file())
            with self.assertRaises(FileExistsError):
                notices.prepare(archive, output)

    def test_wrong_digest_and_unsafe_selected_member_fail_without_output(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.fixture(directory)
            output = Path(directory) / "notices"
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                notices.prepare(archive, output)
            bad = self.fixture(directory, {f"{notices.ROOT}/../escape": b"x"})
            digest = hashlib.sha256(bad.read_bytes()).hexdigest()
            with patch.object(notices, "SHA256", digest):
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    notices.prepare(bad, output)
            self.assertFalse(output.exists())

    def test_missing_license_files_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = self.fixture(directory)
            with tarfile.open(archive, "w:xz") as target:
                data = b"{}"
                member = tarfile.TarInfo(f"{notices.ROOT}/src/qt_attribution.json")
                member.size = len(data)
                target.addfile(member, io.BytesIO(data))
            with patch.object(notices, "SHA256", hashlib.sha256(archive.read_bytes()).hexdigest()):
                with self.assertRaisesRegex(ValueError, "LICENSES"):
                    notices.prepare(archive, Path(directory) / "notices")

    def test_svg_module_and_referenced_attribution_license_are_hashed(self):
        with tempfile.TemporaryDirectory() as directory:
            base = self.fixture(directory, {
                f"{notices.ROOT}/src/3rdparty/demo/qt_attribution.json": b'{"LicenseFile":"COPYING"}',
                f"{notices.ROOT}/src/3rdparty/demo/COPYING": b"upstream copyright"})
            svg = Path(directory) / "svg.tar.xz"
            with tarfile.open(svg, "w:xz") as archive:
                data = b"svg license"
                member = tarfile.TarInfo(f"qtsvg-everywhere-src-{notices.VERSION}/LICENSES/license.txt")
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            with patch.object(notices, "SHA256", hashlib.sha256(base.read_bytes()).hexdigest()), patch.object(notices, "SVG_SHA256", hashlib.sha256(svg.read_bytes()).hexdigest()):
                output = notices.prepare(base, Path(directory) / "notices", svg)
            record = json.loads((output / "source.json").read_text())
            self.assertEqual(set(record["modules"]), {"qtbase", "qtsvg"})
            self.assertEqual((output / "referenced/src/3rdparty/demo/COPYING").read_bytes(), b"upstream copyright")
            for module in record["modules"].values():
                for entry in module["copied_files"]:
                    self.assertEqual(entry["sha256"], hashlib.sha256((output / entry["path"]).read_bytes()).hexdigest())


if __name__ == "__main__":
    unittest.main()
