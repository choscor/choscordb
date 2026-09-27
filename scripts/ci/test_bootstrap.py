"""Security regression tests for the source bootstrap, without network or compilers."""

import hashlib
import io
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import bootstrap_qscintilla as bootstrap


class ArchiveTest(unittest.TestCase):
    def test_macos_architecture_uses_host_arm64_under_translated_python(self):
        with (
            patch.object(bootstrap.platform, "machine", return_value="x86_64"),
            patch.object(
                bootstrap.subprocess, "check_output", return_value="1\n"
            ) as query,
        ):
            self.assertEqual(bootstrap.macos_native_architecture(), "arm64")
        query.assert_called_once_with(
            ["sysctl", "-n", "hw.optional.arm64"],
            text=True,
            stderr=subprocess.DEVNULL,
        )

    def test_macos_architecture_keeps_intel_on_intel_host(self):
        with (
            patch.object(bootstrap.platform, "machine", return_value="x86_64"),
            patch.object(bootstrap.subprocess, "check_output", return_value="0\n"),
        ):
            self.assertEqual(bootstrap.macos_native_architecture(), "x86_64")

    def test_macos_architecture_keeps_intel_when_arm_capability_is_missing(self):
        with (
            patch.object(bootstrap.platform, "machine", return_value="x86_64"),
            patch.object(
                bootstrap.subprocess,
                "check_output",
                side_effect=subprocess.CalledProcessError(1, "sysctl"),
            ),
        ):
            self.assertEqual(bootstrap.macos_native_architecture(), "x86_64")

    def test_bootstrap_qmake_matches_host_under_translated_python(self):
        class StopAfterQmake(Exception):
            pass

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "source.tar.gz"
            archive.touch()

            def query(command, **_kwargs):
                if command[0] == "sysctl":
                    return "1\n"
                if command[0] == "xcrun":
                    return str(root)
                return "6.8.3\n"

            with (
                patch.object(
                    sys,
                    "argv",
                    [
                        "bootstrap_qscintilla.py",
                        "--qmake",
                        str(root / "qmake"),
                        "--prefix",
                        str(root / "prefix"),
                        "--work",
                        str(root / "work"),
                        "--archive",
                        str(archive),
                    ],
                ),
                patch.object(bootstrap.platform, "system", return_value="Darwin"),
                patch.object(bootstrap.platform, "machine", return_value="x86_64"),
                patch.object(bootstrap.subprocess, "check_output", side_effect=query),
                patch.object(bootstrap, "extract_archive"),
                patch.object(bootstrap, "run", side_effect=StopAfterQmake) as execute,
            ):
                with self.assertRaises(StopAfterQmake):
                    bootstrap.main()

            command = execute.call_args.args[0]
            self.assertIn("QMAKE_APPLE_DEVICE_ARCHS=arm64", command)

    def test_macos_without_agl_clears_obsolete_qmake_opengl_libraries(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            self.assertEqual(
                bootstrap.macos_qmake_overrides(sdk), ["QMAKE_LIBS_OPENGL="]
            )
            (sdk / "System/Library/Frameworks/AGL.framework").mkdir(parents=True)
            self.assertEqual(bootstrap.macos_qmake_overrides(sdk), [])

    def test_missing_agl_is_removed_from_generated_link_command_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            makefile = Path(temporary) / "Makefile"
            makefile.write_text(
                "LIBS = -framework QtGui -framework AGL -framework AppKit\n"
                "OTHER = -framework AGLKit\n"
                "INCPATH = -I/System/Library/Frameworks/AGL.framework/Headers\n"
            )
            bootstrap.remove_missing_agl_from_makefile(makefile)
            text = makefile.read_text()
            self.assertEqual(
                text,
                "LIBS = -framework QtGui -framework AppKit\n"
                "OTHER = -framework AGLKit\n"
                "INCPATH = -I/System/Library/Frameworks/AGL.framework/Headers\n",
            )
            bootstrap.remove_missing_agl_from_makefile(makefile)
            self.assertEqual(makefile.read_text(), text)

    def test_wrong_digest_is_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.tar.gz"
            archive.write_bytes(b"untrusted download")
            destination = Path(temporary) / "extracted"
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                bootstrap.extract_archive(archive, destination)
            self.assertFalse(destination.exists())

    def test_even_verified_archive_cannot_escape_destination(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                member = tarfile.TarInfo("../outside")
                member.size = 1
                output.addfile(member, io.BytesIO(b"x"))
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            with patch.object(bootstrap, "SHA256", digest):
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    bootstrap.extract_archive(archive, Path(temporary) / "extract")
            self.assertFalse((Path(temporary) / "outside").exists())

    def test_archive_links_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive, "w:gz") as output:
                member = tarfile.TarInfo("link")
                member.type = tarfile.SYMTYPE
                member.linkname = "/tmp"
                output.addfile(member)
            with patch.object(
                bootstrap, "SHA256", hashlib.sha256(archive.read_bytes()).hexdigest()
            ):
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    bootstrap.extract_archive(archive, Path(temporary) / "extract")


if __name__ == "__main__":
    unittest.main()
