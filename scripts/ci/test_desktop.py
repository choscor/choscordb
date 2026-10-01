"""Native build configuration stays coherent under translated CI tools."""

from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import desktop


class NativeArchitectureTest(unittest.TestCase):
    def test_translated_macos_configures_the_physical_arm_host(self):
        with (
            patch.object(sys, "argv", ["desktop.py", "build"]),
            patch.object(desktop.platform, "system", return_value="Darwin"),
            patch.object(desktop.platform, "machine", return_value="x86_64"),
            patch.object(desktop.subprocess, "check_output", return_value="1\n"),
            patch.object(desktop, "run") as run,
        ):
            desktop.main()
        configure = run.call_args_list[0].args[0]
        self.assertIn("-DCMAKE_OSX_ARCHITECTURES=arm64", configure)
        self.assertNotIn("-DCMAKE_OSX_ARCHITECTURES=x86_64", configure)
        self.assertFalse(any("FLAGS_RELEASE=" in flag for flag in configure))
        self.assertEqual(run.call_args_list[1].args[0][-2:], ["--parallel", "2"])

    def test_intel_macos_keeps_a_native_intel_target(self):
        with (
            patch.object(sys, "argv", ["desktop.py", "build"]),
            patch.object(desktop.platform, "system", return_value="Darwin"),
            patch.object(desktop.platform, "machine", return_value="x86_64"),
            patch.object(desktop.subprocess, "check_output", return_value="0\n"),
            patch.object(desktop, "run") as run,
        ):
            desktop.main()
        self.assertIn("-DCMAKE_OSX_ARCHITECTURES=x86_64", run.call_args_list[0].args[0])

    def test_codeql_preserves_all_release_targets_in_a_separate_cache(self):
        with (
            patch.object(sys, "argv", ["desktop.py", "codeql"]),
            patch.object(desktop.platform, "system", return_value="Darwin"),
            patch.object(desktop.subprocess, "check_output", return_value="1\n"),
            patch.object(desktop, "run") as run,
        ):
            desktop.main()
        configure = run.call_args_list[0].args[0]
        self.assertIn("build/ci/codeql", configure)
        self.assertIn("-DBUILD_TESTING=ON", configure)
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", configure)
        self.assertIn("-DCMAKE_OSX_ARCHITECTURES=arm64", configure)
        self.assertIn("-DCMAKE_CXX_FLAGS_RELEASE=-O0 -DNDEBUG", configure)
        self.assertIn("-DCMAKE_OBJCXX_FLAGS_RELEASE=-O0 -DNDEBUG", configure)
        self.assertIn("-DCMAKE_CXX_COMPILER_LAUNCHER=", configure)
        build = run.call_args_list[1].args[0]
        self.assertEqual(
            build, ["cmake", "--build", "build/ci/codeql", "--parallel", "3"]
        )


class CodeqlPreparationTest(unittest.TestCase):
    def test_preparation_leaves_native_targets_for_traced_build(self):
        with (
            patch.object(sys, "argv", ["desktop.py", "codeql-prepare"]),
            patch.object(desktop.platform, "system", return_value="Darwin"),
            patch.object(desktop.subprocess, "check_output", return_value="0\n"),
            patch.object(desktop, "run") as run,
        ):
            desktop.main()
        self.assertEqual(run.call_count, 2)
        self.assertEqual(
            run.call_args_list[1].args[0],
            [
                "cmake",
                "--build",
                "build/ci/codeql",
                "--target",
                "cargo-build_choscordb_bridge",
            ],
        )

    def test_traced_build_invalidates_prepared_bridge_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "build/ci/codeql/generated/cxxbridge-source/rust/cxx.h"
            header.parent.mkdir(parents=True)
            header.write_text("prepared header")
            with (
                patch.object(sys, "argv", ["desktop.py", "codeql"]),
                patch.object(desktop, "ROOT", root),
                patch.object(desktop.platform, "system", return_value="Darwin"),
                patch.object(desktop.subprocess, "check_output", return_value="0\n"),
                patch.object(desktop, "run") as run,
            ):
                desktop.main()
            self.assertFalse(header.exists())
            self.assertNotIn("--target", run.call_args_list[-1].args[0])
