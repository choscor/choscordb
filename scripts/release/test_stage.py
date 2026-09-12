import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import stage

MACH = b"\xcf\xfa\xed\xfe" + b"fixture"


class StageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n")
        self.qt = self.root / "qt/bin"
        self.qt.mkdir(parents=True)
        (self.qt / "macdeployqt").write_text("fixture")
        self.qsci = self.root / "qsci"
        (self.qsci / "lib").mkdir(parents=True)
        (self.qsci / "lib/libqscintilla2_qt6.15.dylib").write_bytes(MACH)
        notices = self.qsci / "share/licenses/QScintilla"
        notices.mkdir(parents=True)
        (notices / "LICENSE").write_text("fixture license")
        (notices / "source.json").write_text('{"version":"fixture"}')
        self.output = self.root / "stage"
        self.source_manifest = self.root / "source-manifest.json"
        self.source_manifest.write_text(
            '{"format_version":1,"source_kind":"working-tree snapshot",'
            '"git_revision":null,"files":[]}\n'
        )
        self.calls = []
        self.bad_dependency = False
        self.omit_plugin = False
        self.fail_smoke = False
        self.omit_metadata = False
        self.omit_executable = False
        self.omit_license = False

    def run_tool(self, argv, **kwargs):
        argv = list(map(str, argv))
        self.calls.append((argv, kwargs))
        if argv[:2] == ["cmake", "--install"]:
            prefix = Path(argv[argv.index("--prefix") + 1])
            app = prefix / "choscordb.app/Contents"
            (app / "MacOS").mkdir(parents=True)
            exe = app / "MacOS/choscordb"
            if not self.omit_executable:
                exe.write_bytes(MACH)
                exe.chmod(0o755)
            (app / "Resources/licenses").mkdir(parents=True)
            if not self.omit_license:
                (app / "Resources/licenses/LICENSE").write_text("ChoscorDB license")
        elif Path(argv[0]).name == "macdeployqt":
            if self.omit_plugin:
                return ""
            plugin = Path(argv[1]) / "Contents/PlugIns/platforms/libqcocoa.dylib"
            plugin.parent.mkdir(parents=True)
            plugin.write_bytes(MACH)
        elif argv[0] == "otool":
            if argv[1] == "-L":
                dependency = (
                    "/missing/build/liboops.dylib"
                    if self.bad_dependency
                    else "/usr/lib/libSystem.B.dylib"
                )
                return f"{argv[-1]}:\n\t{dependency} (compatibility version 1.0.0, current version 1.0.0)\n"
            return ""
        elif argv[-1] == "--smoke-test":
            env = kwargs["env"]
            self.assertTrue(Path(env["HOME"]).is_dir())
            self.assertNotEqual(env["HOME"], os.environ.get("HOME"))
            self.assertNotIn("DYLD_LIBRARY_PATH", env)
            self.assertNotIn("QT_PLUGIN_PATH", env)
            if self.fail_smoke:
                raise RuntimeError("injected smoke failure")
            if not self.omit_metadata:
                metadata = Path(env["HOME"]) / "data/ChoscorDB/choscordb.sqlite"
                metadata.parent.mkdir(parents=True)
                metadata.write_bytes(b"owned fixture")
        return ""

    def execute(self):
        with patch.object(stage, "run", side_effect=self.run_tool):
            return stage.create_stage(
                self.build,
                self.output,
                self.qt,
                self.qsci,
                self.source_manifest,
                adapter=stage.MacOSAdapter(),
            )

    def test_manifest_install_deploy_smoke_and_no_overwrite(self):
        with patch.dict(
            os.environ,
            {"DYLD_LIBRARY_PATH": "development", "QT_PLUGIN_PATH": "development"},
        ):
            self.execute()
        manifest = json.loads((self.output / "manifest.json").read_text())
        self.assertEqual(
            manifest["source_candidate"]["manifest_sha256"],
            hashlib.sha256(self.source_manifest.read_bytes()).hexdigest(),
        )
        self.assertEqual(
            manifest["source_candidate"]["source_kind"], "working-tree snapshot"
        )
        paths = [entry["path"] for entry in manifest["files"]]
        self.assertEqual(paths, sorted(paths))
        for entry in manifest["files"]:
            data = (self.output / entry["path"]).read_bytes()
            self.assertEqual(entry["size"], len(data))
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
        self.assertIn(
            "choscordb.app/Contents/Resources/licenses/QScintilla/source.json", paths
        )
        checksum = (self.output / "SHA256SUMS").read_text().split()[0]
        self.assertEqual(
            checksum,
            hashlib.sha256((self.output / "manifest.json").read_bytes()).hexdigest(),
        )
        self.assertTrue(
            any(args[:2] == ["cmake", "--install"] for args, _ in self.calls)
        )
        self.assertTrue(
            any(Path(args[0]).name == "macdeployqt" for args, _ in self.calls)
        )
        self.assertTrue(any(args[-1] == "--smoke-test" for args, _ in self.calls))
        smoke = next(args for args, _ in self.calls if args[-1] == "--smoke-test")
        self.assertTrue(Path(smoke[0]).resolve().is_relative_to(self.output.resolve()))
        with self.assertRaises(FileExistsError):
            self.execute()

    def test_bad_dependencies_fail_without_partial_output(self):
        self.bad_dependency = True
        with self.assertRaises(ValueError):
            self.execute()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".stage-*")))

    def test_escaping_symlink_rejected(self):
        tree = self.root / "tree"
        tree.mkdir()
        (tree / "escape").symlink_to(self.build, target_is_directory=True)
        with self.assertRaises(ValueError):
            stage.regular_files(tree)

    def test_missing_plugin_and_smoke_failure_leave_no_output(self):
        self.omit_plugin = True
        with self.assertRaisesRegex(ValueError, "plugin"):
            self.execute()
        self.assertFalse(self.output.exists())
        self.omit_plugin = False
        self.fail_smoke = True
        with self.assertRaisesRegex(RuntimeError, "smoke failure"):
            self.execute()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".stage-*")))

    def test_debug_build_and_missing_owned_metadata_are_rejected(self):
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
        with self.assertRaisesRegex(ValueError, "Release"):
            self.execute()
        self.assertFalse(self.output.exists())
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n")
        self.omit_metadata = True
        with self.assertRaisesRegex(ValueError, "temporary metadata"):
            self.execute()
        self.assertFalse(self.output.exists())

    def test_invalid_source_candidate_manifest_is_rejected(self):
        self.source_manifest.write_text("{}")
        with self.assertRaisesRegex(ValueError, "Source-candidate manifest"):
            self.execute()
        self.assertFalse(self.output.exists())

    def test_missing_executable_license_and_qscintilla_fail_closed(self):
        self.omit_executable = True
        with self.assertRaisesRegex(ValueError, "executable"):
            self.execute()
        self.omit_executable = False
        self.omit_license = True
        with self.assertRaisesRegex(ValueError, "license"):
            self.execute()
        self.omit_license = False
        (self.qsci / "lib/libqscintilla2_qt6.15.dylib").unlink()
        with self.assertRaisesRegex(ValueError, "QScintilla runtime"):
            self.execute()
        self.assertFalse(self.output.exists())

    def test_loader_rpath_resolution_requires_actual_bundled_macho(self):
        prefix = self.root / "dependency-fixture"
        self.run_tool(["cmake", "--install", self.build, "--prefix", prefix])
        app = prefix / "choscordb.app"
        self.run_tool([self.qt / "macdeployqt", app])
        library = app / "Contents/Frameworks/libfixture.dylib"
        library.parent.mkdir()
        library.write_bytes(MACH)
        executable = app / "Contents/MacOS/choscordb"

        def inspect(argv, **kwargs):
            if str(argv[1]) == "-l":
                return "cmd LC_RPATH\npath @executable_path/../Frameworks (offset 12)\n"
            if Path(argv[-1]).resolve() == executable.resolve():
                return (
                    "binary:\n\t@rpath/libfixture.dylib (compatibility version 1.0.0)\n"
                )
            return (
                "binary:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
            )

        with patch.object(stage, "run", side_effect=inspect):
            self.assertEqual(
                stage.MacOSAdapter().validate(prefix), executable.resolve()
            )
            library.write_text("not an actual dylib")
            with self.assertRaisesRegex(ValueError, "Unresolved"):
                stage.MacOSAdapter().validate(prefix)

    def test_internal_framework_symlink_is_allowed_but_not_double_manifested(self):
        tree = self.root / "tree"
        version = tree / "Framework/Versions/A"
        version.mkdir(parents=True)
        (version / "binary").write_bytes(MACH)
        (tree / "Framework/Versions/Current").symlink_to("A", target_is_directory=True)
        (tree / "Framework/binary").symlink_to("Versions/Current/binary")
        self.assertEqual(stage.regular_files(tree), [(version / "binary").resolve()])

    def test_absolute_rpath_inside_temporary_stage_is_rejected_before_rename(self):
        prefix = self.root / "temporary-stage"
        self.run_tool(["cmake", "--install", self.build, "--prefix", prefix])
        app = prefix / "choscordb.app"
        self.run_tool([self.qt / "macdeployqt", app])

        def inspect(argv, **kwargs):
            if str(argv[1]) == "-l":
                return f"cmd LC_RPATH\npath {app / 'Contents/Frameworks'} (offset 12)\n"
            return f"{argv[-1]}:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"

        with patch.object(stage, "run", side_effect=inspect):
            with self.assertRaisesRegex(ValueError, "Non-relocatable rpath"):
                stage.MacOSAdapter().validate(prefix)

    def test_directory_scan_failure_is_never_silently_omitted(self):
        tree = self.root / "tree"
        tree.mkdir()
        real_walk = os.walk

        def fail_tree(path, *args, **kwargs):
            if Path(path).name == "tree":
                error = PermissionError("injected unreadable stage directory")
                if callback := kwargs.get("onerror"):
                    callback(error)
                return iter(())
            return real_walk(path, *args, **kwargs)

        with patch.object(stage.os, "walk", side_effect=fail_tree):
            with self.assertRaisesRegex(PermissionError, "injected unreadable"):
                stage.regular_files(tree)


if __name__ == "__main__":
    unittest.main()
