"""Exercise the production updater configuration with CMake's actual regex engine."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PUBLIC_KEY = "tWwf5+4hAmilViw6CrynbGYUr5ISGt/4OOAKR5jm+0o="


@unittest.skipUnless(shutil.which("cmake"), "requires CMake")
class WindowsLinuxUpdaterConfigurationTests(unittest.TestCase):
    def configure(self, key, base="https://choscor.github.io/choscordb/updates"):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "validate.cmake"
            script.write_text(
                f"""cmake_minimum_required(VERSION 3.24)
set(APPLE FALSE)
set(BUILD_TESTING FALSE)
set(CHOSCORDB_CROSS_PLATFORM_UPDATER ON)
set(CHOSCORDB_UPDATE_PUBLIC_KEY [=[{key}]=])
set(CHOSCORDB_UPDATE_BASE_URL [=[{base}]=])
set(CHOSCORDB_UPDATE_REPOSITORY "choscor/choscordb")
function(target_sources)
endfunction()
function(_choscordb_register_first_party_target)
endfunction()
function(target_link_libraries)
endfunction()
function(target_compile_definitions)
endfunction()
include([=[{(ROOT / "cmake/WindowsLinuxUpdater.cmake").as_posix()}]=])
choscordb_configure_windows_linux_updater(fixture)
""",
                encoding="utf-8",
            )
            return subprocess.run(
                ["cmake", "-P", script], capture_output=True, text=True, check=False
            )

    def test_accepts_the_pinned_ed25519_public_key(self):
        result = self.configure(PUBLIC_KEY)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_wrong_length_characters_and_padding(self):
        for key in (
            "",
            PUBLIC_KEY[:-1],
            PUBLIC_KEY + "=",
            PUBLIC_KEY[:-1] + "A",
            PUBLIC_KEY[:-2] + "==",
            "!" + PUBLIC_KEY[1:],
        ):
            with self.subTest(key=key):
                result = self.configure(key)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("dedicated raw Ed25519 public key", result.stderr)

    def test_rejects_a_pages_url_from_another_repository(self):
        result = self.configure(PUBLIC_KEY, "https://example.github.io/fork/updates")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must belong to CHOSCORDB_UPDATE_REPOSITORY", result.stderr)


if __name__ == "__main__":
    unittest.main()
