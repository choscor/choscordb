"""Observe the real CMake project version before native dependency discovery."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildVersionTest(unittest.TestCase):
    def configure_version(self, version):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copy2(ROOT / "CMakeLists.txt", root)
            shutil.copytree(ROOT / "cmake", root / "cmake")
            (root / "Cargo.toml").write_text(
                "[workspace]\nmembers = []\n[workspace.package]\n"
                f'version = "{version}"\nedition = "2024"\n'
            )
            hook = root / "observe.cmake"
            hook.write_text(
                'file(WRITE "${CMAKE_BINARY_DIR}/observed.txt" "${PROJECT_VERSION}")\n'
                'message(FATAL_ERROR "Version observation complete")\n'
            )
            result = subprocess.run(
                [
                    "cmake",
                    "-S",
                    str(root),
                    "-B",
                    str(root / "build"),
                    f"-DCMAKE_PROJECT_INCLUDE={hook}",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            observed = root / "build/observed.txt"
            return (
                observed.read_text() if observed.exists() else None,
                result.stdout + result.stderr,
            )

    def test_cargo_workspace_version_drives_native_project(self):
        for version in ("2.3.4", "10.20.30"):
            with self.subTest(version=version):
                observed, diagnostic = self.configure_version(version)
                self.assertEqual(observed, version, diagnostic)

    def test_prerelease_and_noncanonical_versions_fail_before_build(self):
        for version in ("1.2.3-beta.1", "01.2.3", "1.2", "1.2.3.4"):
            with self.subTest(version=version):
                observed, diagnostic = self.configure_version(version)
                self.assertIsNone(observed, diagnostic)
                self.assertIn("must be a stable X.Y.Z version", diagnostic)


if __name__ == "__main__":
    unittest.main()
