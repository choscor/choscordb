import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import source_archive


class SourceArchiveTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "source"
        self.root.mkdir()
        for name in source_archive.SOURCE_ROOTS:
            (self.root / name).mkdir()
            (self.root / name / "example.txt").write_text(name)
        for name in source_archive.ROOT_FILES:
            (self.root / name).write_text(name)

    def generate(self, name="output"):
        output = Path(self.temporary.name) / name
        source_archive.create_candidate(self.root, output)
        return output

    def test_independent_manifest_checksums_and_reproducibility(self):
        (self.root / "crates/build.rs").write_text("fn main() {}")
        (self.root / "docs/design.png").write_bytes(b"image")
        first = self.generate()
        manifest = json.loads((first / "manifest.json").read_text())
        self.assertEqual(manifest["source_kind"], "working-tree snapshot")
        self.assertIsNone(manifest["git_revision"])
        expected = {entry["path"]: entry for entry in manifest["files"]}
        with tarfile.open(first / "choscordb-source.tar.gz", "r:gz") as archive:
            members = archive.getmembers()
            self.assertEqual([m.name for m in members], sorted(m.name for m in members))
            self.assertEqual(
                set(expected),
                {m.name.removeprefix("choscordb-source/") for m in members},
            )
            for member in members:
                self.assertTrue(member.isfile())
                self.assertEqual((member.uid, member.gid, member.mtime), (0, 0, 0))
                self.assertEqual((member.uname, member.gname), ("", ""))
                data = archive.extractfile(member).read()
                entry = expected[member.name.removeprefix("choscordb-source/")]
                self.assertEqual(entry["size"], len(data))
                self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
        self.assertIn("crates/build.rs", expected)
        self.assertNotIn("docs/design.png", expected)
        for line in (first / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(
                digest, hashlib.sha256((first / name).read_bytes()).hexdigest()
            )
        os.utime(self.root / "Cargo.lock", (123456, 123456))
        second = self.generate("second")
        for name in ["choscordb-source.tar.gz", "manifest.json", "SHA256SUMS"]:
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())

    def test_excludes_local_artifacts_but_preserves_build_script(self):
        for path in [
            "build/binary",
            "target/binary",
            ".git/config",
            "crates/target/binary",
            "scripts/__pycache__/a.pyc",
            "scripts/.venv/private",
            "docs/local.sqlite",
            "docs/local.sqlite-wal",
            "docs/local.db-shm",
            "docs/file~",
            "desktop/file.cpp.swp",
            "scripts/.env",
            "scripts/.env.local",
            "docs/screenshot.PNG",
            "scripts/install.log",
            "scripts/install.LOG",
            "scripts/private.key",
            "scripts/private.pem",
            "scripts/private.p12",
            "scripts/private.pfx",
            "secret.txt",
        ]:
            file = self.root / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("excluded")
        manifest = json.loads((self.generate() / "manifest.json").read_text())
        self.assertFalse(
            any(
                "excluded" in (self.root / row["path"]).read_text()
                for row in manifest["files"]
            )
        )

    def test_rejects_symlinks_missing_inputs_and_output_inside_sources(self):
        link = self.root / "docs/link"
        link.symlink_to(self.root / "LICENSE")
        with self.assertRaises(ValueError):
            self.generate()
        link.unlink()
        with self.assertRaises(ValueError):
            source_archive.create_candidate(self.root, self.root / "docs/generated")
        (self.root / "Cargo.lock").unlink()
        with self.assertRaises(ValueError):
            self.generate()

    def test_existing_outputs_are_not_overwritten(self):
        output = self.generate()
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        with self.assertRaises(FileExistsError):
            source_archive.create_candidate(self.root, output)
        self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})

    def test_missing_required_tree_and_directory_symlinks_are_rejected(self):
        (self.root / "docs/example.txt").unlink()
        (self.root / "docs").rmdir()
        with self.assertRaises(ValueError):
            self.generate()
        (self.root / "docs").symlink_to(self.root / "crates", target_is_directory=True)
        with self.assertRaises(ValueError):
            self.generate()

    def test_ancestor_replaced_after_discovery_never_archives_external_bytes(self):
        external = Path(self.temporary.name) / "external"
        external.mkdir()
        (external / "example.txt").write_text("external secret must never be read")

        def replace_directory(_root):
            (self.root / "docs").rename(self.root / "original-docs")
            (self.root / "docs").symlink_to(external, target_is_directory=True)
            return None

        with patch.object(
            source_archive, "git_revision", side_effect=replace_directory
        ):
            with self.assertRaises((ValueError, OSError)):
                self.generate()
        self.assertFalse((Path(self.temporary.name) / "output").exists())

    def test_compiled_artifacts_are_excluded_inside_source_trees(self):
        for name in [
            "file.o",
            "file.obj",
            "lib.a",
            "file.lib",
            "lib.so",
            "lib.so.1.2",
            "lib.dylib",
            "file.dll",
            "file.exe",
            "file.pdb",
            "FILE.DLL",
            "app.dSYM/Contents/Resources/DWARF/app",
        ]:
            file = self.root / "desktop" / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"compiled artifact")
        entries = json.loads((self.generate() / "manifest.json").read_text())["files"]
        self.assertEqual(
            [
                entry["path"]
                for entry in entries
                if entry["path"].startswith("desktop/")
            ],
            ["desktop/example.txt"],
        )

    def test_missing_secure_open_support_fails_closed(self):
        with patch.object(source_archive.os, "supports_dir_fd", set()):
            with self.assertRaisesRegex(ValueError, "Secure source traversal"):
                self.generate()
        self.assertFalse((Path(self.temporary.name) / "output").exists())

    def test_directory_scan_failure_is_never_silently_omitted(self):
        real_walk = os.walk

        def fail_docs(path, *args, **kwargs):
            if Path(path).name == "docs":
                error = PermissionError("injected unreadable source directory")
                if callback := kwargs.get("onerror"):
                    callback(error)
                return iter(())
            return real_walk(path, *args, **kwargs)

        with patch.object(source_archive.os, "walk", side_effect=fail_docs):
            with self.assertRaisesRegex(PermissionError, "injected unreadable"):
                self.generate()

    def test_cmake_build_outputs_excluded_but_cmake_sources_preserved(self):
        artifacts = [
            "CMakeFiles/compiler.txt",
            "cmake-build-debug/generated.txt",
            "cmake-build-release/generated.txt",
            "CMakeCache.txt",
            "cmake_install.cmake",
            "compile_commands.json",
            "install_manifest.txt",
        ]
        for name in artifacts:
            path = self.root / "desktop" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("generated build artifact")
        (self.root / "cmake/Rust.cmake").write_text("# source CMake helper")
        (self.root / "desktop/CMakeLists.txt").write_text("# source build definition")
        paths = {
            entry["path"]
            for entry in json.loads((self.generate() / "manifest.json").read_text())[
                "files"
            ]
        }
        self.assertFalse(paths.intersection("desktop/" + name for name in artifacts))
        self.assertIn("cmake/Rust.cmake", paths)
        self.assertIn("desktop/CMakeLists.txt", paths)

    @unittest.skipUnless(shutil.which("git"), "git unavailable")
    def test_git_head_is_recorded_without_claiming_clean_committed_contents(self):
        def git(*args):
            return subprocess.check_output(
                ["git", "-C", str(self.root), *args], text=True, stderr=subprocess.PIPE
            ).strip()

        git("init")
        git("add", "LICENSE")
        git(
            "-c",
            "user.name=Source Fixture",
            "-c",
            "user.email=fixture@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-m",
            "Fixture commit",
        )
        expected = git("rev-parse", "HEAD")
        (self.root / "LICENSE").write_text("uncommitted license fixture contents")
        manifest = json.loads((self.generate() / "manifest.json").read_text())
        self.assertEqual(manifest["git_revision"], expected)
        self.assertEqual(manifest["source_kind"], "working-tree snapshot")
        self.assertIn("uncommitted", manifest["provenance_note"])
        license_entry = next(
            entry for entry in manifest["files"] if entry["path"] == "LICENSE"
        )
        self.assertEqual(
            license_entry["sha256"],
            hashlib.sha256((self.root / "LICENSE").read_bytes()).hexdigest(),
        )


if __name__ == "__main__":
    unittest.main()
