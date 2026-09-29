"""The Windows candidate smoke never touches an existing user installation."""

import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
import cross_platform


@unittest.skipUnless(os.name == "posix", "portable script fixture")
class InstallSmokeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.install = self.root / "Programs/ChoscorDB"
        self.installer = self.root / "setup.exe"
        self.marker = self.root / "installer-ran"
        self.installer.write_text(
            "#!/bin/sh\n"
            f"touch '{self.marker}'\n"
            f"mkdir -p '{self.install}'\n"
            f"printf '#!/bin/sh\\nexit 0\\n' > '{self.install / 'choscordb.exe'}'\n"
            f"printf '#!/bin/sh\\nrm -rf \"{self.install}\"\\n' > '{self.install / 'Uninstall.exe'}'\n"
            f"chmod +x '{self.install / 'choscordb.exe'}' '{self.install / 'Uninstall.exe'}'\n"
        )
        self.installer.chmod(0o755)

    def test_refuses_preexisting_install_before_invoking_installer(self):
        self.install.mkdir(parents=True)
        previous = self.install / "choscordb.exe"
        previous.write_bytes(b"old user installation")
        with self.assertRaisesRegex(ValueError, "existing per-user installation"):
            cross_platform.smoke_windows_installer(
                self.installer, self.install, os.environ.copy()
            )
        self.assertEqual(previous.read_bytes(), b"old user installation")
        self.assertFalse(self.marker.exists())

    def test_fresh_install_is_smoked_and_removed(self):
        cross_platform.smoke_windows_installer(
            self.installer, self.install, os.environ.copy()
        )
        self.assertTrue(self.marker.exists())
        self.assertFalse(self.install.exists())


if __name__ == "__main__":
    unittest.main()
