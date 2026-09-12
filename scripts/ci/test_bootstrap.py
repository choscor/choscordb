"""Security regression tests for the source bootstrap, without network or compilers."""

import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import bootstrap_qscintilla as bootstrap


class ArchiveTest(unittest.TestCase):
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
