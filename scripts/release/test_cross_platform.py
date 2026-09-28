"""Packaging safety checks that run without native toolchains."""

import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent))
import cross_platform


class PackagingSafetyTests(unittest.TestCase):
    def test_stale_cmake_version_cannot_be_relabelled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                "CMAKE_PROJECT_VERSION:STATIC=0.0.1\n"
            )
            with (
                patch("cross_platform.require_clean_source"),
                patch("cross_platform.require_release_configuration"),
            ):
                with self.assertRaisesRegex(ValueError, "build version differs"):
                    cross_platform.require_inputs(
                        build, root / "qt", root / "qsci", root / "output"
                    )

    def test_linux_tool_hash_mismatch_refuses_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            # A bad downloaded tool must be removed before it can be invoked.
            with patch(
                "cross_platform.urllib.request.urlopen",
                return_value=io.BytesIO(b"wrong"),
            ):
                with self.assertRaisesRegex(ValueError, "digest mismatch"):
                    cross_platform.download_linux_tools(Path(directory))
            self.assertFalse(list(Path(directory).glob("*.AppImage")))

    def test_inventory_rejects_symlink_leaving_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "package"
            package.mkdir()
            (package / "escape").symlink_to("..")
            with self.assertRaisesRegex(ValueError, "escapes stage"):
                cross_platform.write_inventory(package, root / "inventory.json")

    def test_qt_notice_tampering_is_rejected_before_cargo_collection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "stage/share/licenses/choscordb").mkdir(parents=True)
            (root / "qsci/share/licenses/QScintilla").mkdir(parents=True)
            qt = root / "qt-notices"
            qt.mkdir()
            (qt / "LICENSE").write_text("changed")
            (qt / "source.json").write_text(
                json.dumps(
                    {
                        "version": "6.8.3",
                        "modules": {
                            "qtbase": {
                                "sha256": cross_platform.QTBASE_SHA256,
                                "copied_files": [
                                    {"path": "LICENSE", "sha256": "0" * 64}
                                ],
                            },
                            "qtsvg": {
                                "sha256": cross_platform.QTSVG_SHA256,
                                "copied_files": [],
                            },
                        },
                    }
                )
            )
            with self.assertRaisesRegex(ValueError, "hashes or file set"):
                cross_platform.add_notices(
                    root / "stage", root / "qsci", qt, "x86_64-unknown-linux-gnu"
                )


if __name__ == "__main__":
    unittest.main()
