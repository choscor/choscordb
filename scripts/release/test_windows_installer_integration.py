"""Windows runner rehearsal of NSIS upgrade rollback and startup readiness."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


SOURCE = Path(__file__).resolve().parent


@unittest.skipUnless(os.name == "nt", "requires a disposable Windows runner")
class WindowsInstallerIntegration(unittest.TestCase):
    def test_immediate_exit_restores_old_then_ready_launch_replaces_it(self):
        import winreg

        self.assertIsNotNone(shutil.which("makensis.exe"))
        self.assertIsNotNone(shutil.which("cl.exe"))
        root = Path(os.environ["LOCALAPPDATA"]) / "Programs/ChoscorDB"
        backup = root.with_name(root.name + ".previous")
        marker = (
            Path(os.environ["LOCALAPPDATA"]) / "ChoscorDB/update-install-failure.txt"
        )
        cleanup_marker = (
            Path(os.environ["LOCALAPPDATA"]) / "ChoscorDB/update-cleanup-pending.txt"
        )
        shortcut = (
            Path(os.environ["APPDATA"])
            / "Microsoft/Windows/Start Menu/Programs"
            / "ChoscorDB.lnk"
        )
        registration = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\ChoscorDB"

        def registered():
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, registration):
                    return True
            except FileNotFoundError:
                return False

        self.assertFalse(root.exists(), "test requires an unused Windows account")
        self.assertFalse(backup.exists(), "test requires no prior update backup")
        self.assertFalse(marker.exists(), "test requires no prior failure marker")
        self.assertFalse(
            cleanup_marker.exists(), "test requires no prior cleanup marker"
        )
        self.assertFalse(shortcut.exists(), "test requires no prior Start Menu entry")
        self.assertFalse(registered(), "test requires no prior uninstall registration")

        def cleanup_owned():
            if root.exists():
                shutil.rmtree(root)
            if backup.exists():
                shutil.rmtree(backup)
            marker.unlink(missing_ok=True)
            cleanup_marker.unlink(missing_ok=True)
            shortcut.unlink(missing_ok=True)
            if registered():
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, registration)

        self.addCleanup(cleanup_owned)
        with tempfile.TemporaryDirectory(prefix="choscordb-nsis-test-") as directory:
            work = Path(directory)

            def compile_exe(name, body):
                folder = work / name
                folder.mkdir()
                source = folder / "main.c"
                source.write_text("#include <windows.h>\n" + body)
                subprocess.run(
                    ["cl.exe", "/nologo", "/W4", "/Fe:choscordb.exe", str(source)],
                    cwd=folder,
                    check=True,
                    capture_output=True,
                    timeout=60,
                )
                return folder / "choscordb.exe"

            old = compile_exe(
                "old",
                r"""
int main(void) {
  wchar_t path[MAX_PATH]; DWORD n;
  HANDLE h;
  n = GetEnvironmentVariableW(L"CHOSCORDB_TEST_OLD_LAUNCHED", path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return 1;
  h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 2;
  CloseHandle(h);
  return 0;
}
""",
            )
            exits = compile_exe("exits", "int main(void) { return 7; }\n")
            ready = compile_exe(
                "ready",
                r"""
int main(void) {
  wchar_t path[MAX_PATH]; DWORD n;
  HANDLE h; DWORD written;
  n = GetEnvironmentVariableW(L"CHOSCORDB_UPDATE_READY_FILE", path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return 8;
  h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return 9;
  if (!WriteFile(h, "ready\n", 6, &written, NULL) || written != 6) return 10;
  CloseHandle(h);
  Sleep(5000);
  return 0;
}
""",
            )

            def installer(payload, name):
                output = work / name
                subprocess.run(
                    [
                        "makensis.exe",
                        "/V2",
                        f"/DPAYLOAD={payload.parent}",
                        f"/DOUTPUT={output}",
                        f"/DWAIT_SCRIPT={SOURCE / 'windows_wait_for_start.ps1'}",
                        str(SOURCE / "windows_installer.nsi"),
                    ],
                    check=True,
                    capture_output=True,
                    timeout=60,
                )
                return output

            bad_setup = installer(exits, "ChoscorDB-test-bad-setup.exe")
            good_setup = installer(ready, "ChoscorDB-test-good-setup.exe")
            old_bytes = old.read_bytes()
            old_launched = work / "old-launched"
            installer_env = {
                **os.environ,
                "CHOSCORDB_TEST_OLD_LAUNCHED": str(old_launched),
            }
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, registration):
                pass
            blocked = subprocess.run([bad_setup, "/S"], env=installer_env, timeout=15)
            self.assertNotEqual(blocked.returncode, 0)
            self.assertTrue(registered())
            self.assertFalse(root.exists())
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, registration)
            marker.unlink(missing_ok=True)
            # A clean account must pass uninstall-key enumeration promptly.
            first_failed = subprocess.run(
                [bad_setup, "/S"], env=installer_env, timeout=30
            )
            self.assertNotEqual(first_failed.returncode, 0)
            self.assertFalse(root.exists())
            self.assertFalse(registered())
            self.assertFalse(shortcut.exists())
            self.assertTrue(marker.is_file())
            marker.unlink()
            root.mkdir(parents=True)
            shutil.copy2(old, root / "choscordb.exe")
            try:
                blocker = subprocess.Popen(
                    [
                        "powershell.exe",
                        "-NoProfile",
                        "-Command",
                        "Start-Sleep -Seconds 1",
                    ]
                )
                failed = subprocess.run(
                    [bad_setup, "/S", f"/WAITPID={blocker.pid}"],
                    env=installer_env,
                    timeout=90,
                )
                blocker.wait(timeout=10)
                self.assertNotEqual(failed.returncode, 0)
                self.assertEqual((root / "choscordb.exe").read_bytes(), old_bytes)
                self.assertFalse(backup.exists())
                self.assertEqual(list(root.parent.glob("ChoscorDB.pending.*")), [])
                self.assertTrue(marker.is_file())
                for _ in range(20):
                    if old_launched.exists():
                        break
                    time.sleep(0.1)
                self.assertTrue(old_launched.exists())
                upgraded = subprocess.run(
                    [good_setup, "/S"], env=installer_env, timeout=90
                )
                self.assertEqual(upgraded.returncode, 0)
                self.assertEqual(
                    hashlib.sha256((root / "choscordb.exe").read_bytes()).digest(),
                    hashlib.sha256(ready.read_bytes()).digest(),
                )
                self.assertFalse(backup.exists())
                self.assertEqual(list(root.parent.glob("ChoscorDB.pending.*")), [])
                self.assertFalse(marker.exists())
                time.sleep(5)
                backup.mkdir()
                shutil.copy2(old, backup / "choscordb.exe")
                cleanup_marker.write_text("ready-upgrade-backup")
                resumed = subprocess.run(
                    [good_setup, "/S"], env=installer_env, timeout=90
                )
                self.assertEqual(resumed.returncode, 0)
                self.assertFalse(backup.exists())
                self.assertFalse(cleanup_marker.exists())
            finally:
                time.sleep(5)
                if (root / "Uninstall.exe").is_file():
                    subprocess.run([root / "Uninstall.exe", "/S"], timeout=60)
                for _ in range(20):
                    if not root.exists():
                        break
                    time.sleep(0.5)
                if root.exists():
                    shutil.rmtree(root)
                if backup.exists():
                    shutil.rmtree(backup)
                marker.unlink(missing_ok=True)
                cleanup_marker.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
