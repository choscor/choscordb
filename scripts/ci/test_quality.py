"""Tests for the repository-owned quality command interface."""

import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import quality


class QualityCommandTest(unittest.TestCase):
    def run_stage(self, stage):
        calls = []

        def record(command, **_kwargs):
            calls.append([str(argument) for argument in command])

        with (
            patch.object(
                quality, "require_tool", side_effect=lambda tool, **_kwargs: tool
            ),
            patch.object(quality, "clang_format_tool", return_value="clang-format"),
            patch.object(quality, "actionlint_tool", return_value="actionlint"),
            patch.object(quality, "cargo_deny_tool", return_value="cargo-deny"),
            patch.object(quality, "require_native_dependencies"),
            patch.object(quality, "run_command", side_effect=record),
        ):
            self.assertEqual(quality.main([stage]), 0)
        return calls

    def test_fast_runs_deterministic_format_static_and_test_gates(self):
        commands = self.run_stage("fast")
        self.assertEqual(commands[0][:3], ["clang-format", "--dry-run", "--Werror"])
        self.assertEqual(commands[0][3:], [str(path) for path in quality.cpp_files()])
        self.assertEqual(
            commands[1:],
            [
                [quality.PYTHON, "scripts/ci/cpp_size.py"],
                [quality.PYTHON, "scripts/ci/qss_policy.py"],
                [quality.PYTHON, "scripts/ci/ui_policy.py"],
                [quality.PYTHON, "scripts/ci/ui_consistency.py"],
                ["ruff", "check", "scripts", "examples"],
                ["ruff", "format", "--check", "scripts", "examples"],
                ["actionlint", "-color"],
                [
                    quality.PYTHON,
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/ci",
                    "-p",
                    "test_*.py",
                ],
                [
                    quality.PYTHON,
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/release",
                    "-p",
                    "test_*.py",
                ],
                ["cargo", "fmt", "--all", "--", "--check"],
                [
                    "cargo",
                    "check",
                    "--workspace",
                    "--all-targets",
                    "--all-features",
                    "--locked",
                ],
                [
                    "cargo",
                    "clippy",
                    "--workspace",
                    "--all-targets",
                    "--all-features",
                    "--locked",
                    "--",
                    "-D",
                    "warnings",
                ],
                ["cargo", "test", "--workspace", "--all-features", "--locked"],
                ["cargo", "deny", "--locked", "check"],
            ],
        )

    def test_full_adds_native_build_and_ctest_via_desktop(self):
        commands = self.run_stage("full")
        self.assertEqual(
            commands[-2:],
            [
                [quality.PYTHON, "scripts/ci/desktop.py", "build"],
                [quality.PYTHON, "scripts/ci/desktop.py", "test"],
            ],
        )

    def test_each_gate_is_a_focused_stage(self):
        expected = {
            "python-tests": 2,
            "rust-format": 1,
            "rust-check": 1,
            "rust-clippy": 1,
            "rust-tests": 1,
            "python-lint": 1,
            "python-format": 1,
            "cpp-format": 1,
            "cpp-size": 1,
            "actionlint": 1,
            "cargo-deny": 1,
            "native-dependencies": 1,
            "native-build": 1,
            "native-tests": 1,
        }
        for stage, count in expected.items():
            with self.subTest(stage=stage):
                self.assertEqual(len(self.run_stage(stage)), count)

    def test_cpp_scope_is_only_hand_written_sources_and_headers(self):
        files = quality.cpp_files()
        self.assertTrue(files)
        self.assertEqual(files, sorted(files))
        self.assertTrue(all(path.parts[0] in {"desktop", "tests"} for path in files))
        suffixes = {".c", ".cc", ".cpp", ".cxx", ".mm", ".h", ".hh", ".hpp", ".hxx"}
        self.assertTrue(all(path.suffix in suffixes for path in files))

    def test_native_updater_sources_are_in_formatting_scope(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "desktop").mkdir()
            (root / "desktop/updater.mm").write_text("// Objective-C++\n")
            with patch.object(quality, "ROOT", root):
                self.assertEqual(quality.cpp_files(), [Path("desktop/updater.mm")])

    def test_commands_are_printed_before_execution(self):
        output = io.StringIO()
        with (
            patch("subprocess.run") as subprocess_run,
            contextlib.redirect_stdout(output),
        ):
            quality.run_command(["cargo", "fmt", "--all"])
        subprocess_run.assert_called_once_with(
            ["cargo", "fmt", "--all"], check=True, cwd=quality.ROOT
        )
        self.assertIn("Running: cargo fmt --all", output.getvalue())

    def test_missing_tool_is_actionable(self):
        with patch("shutil.which", return_value=None):
            with self.assertRaisesRegex(quality.QualityError, "pip install"):
                quality.require_tool(
                    "ruff",
                    install="python -m pip install -r scripts/ci/requirements.txt",
                )

    def test_wrong_clang_format_major_is_rejected(self):
        completed = subprocess.CompletedProcess(
            ["clang-format", "--version"], 0, stdout="clang-format version 22.1.0\n"
        )
        with (
            patch(
                "shutil.which",
                side_effect=lambda tool: (
                    "/usr/bin/clang-format" if tool == "clang-format" else None
                ),
            ),
            patch("subprocess.run", return_value=completed),
        ):
            with self.assertRaisesRegex(quality.QualityError, "LLVM 23"):
                quality.clang_format_tool()

    def test_wrong_pinned_tool_version_is_rejected_with_install_guidance(self):
        completed = subprocess.CompletedProcess(
            ["actionlint", "-version"], 0, stdout="v1.7.70\nbuilt with go1.23.6\n"
        )
        with (
            patch("shutil.which", return_value="/usr/bin/actionlint"),
            patch("subprocess.run", return_value=completed),
            self.assertRaisesRegex(quality.QualityError, "@v1.7.7"),
        ):
            quality.actionlint_tool()

    def test_subprocess_failure_returns_nonzero(self):
        with (
            patch.object(quality, "require_tool", return_value="ruff"),
            patch.object(
                quality,
                "run_command",
                side_effect=subprocess.CalledProcessError(7, ["ruff"]),
            ),
        ):
            self.assertEqual(quality.main(["python-lint"]), 7)

    def test_missing_native_dependencies_are_actionable(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(quality, "ROOT", quality.Path(directory)),
            self.assertRaisesRegex(quality.QualityError, "native-dependencies"),
        ):
            quality.require_native_dependencies()

    def test_partial_native_dependencies_are_rejected_but_complete_layout_passes(self):
        with (
            tempfile.TemporaryDirectory() as directory,
            patch.object(quality, "ROOT", quality.Path(directory)),
        ):
            tools = quality.ROOT / "build/ci"
            qt = tools / "qt" / quality.QT_VERSION / quality.qt_directory()
            qsci = tools / "qscintilla"
            qt.mkdir(parents=True)
            qsci.mkdir(parents=True)
            with self.assertRaisesRegex(quality.QualityError, "qmake"):
                quality.require_native_dependencies()
            (qt / "bin").mkdir()
            (qt / "bin" / quality.qmake_name()).touch()
            (qsci / "include/Qsci").mkdir(parents=True)
            (qsci / "include/Qsci/qsciscintilla.h").touch()
            (qsci / "lib").mkdir()
            (qsci / "lib/libqscintilla2.a").touch()
            quality.require_native_dependencies()


class RequirementsTest(unittest.TestCase):
    def test_ruff_is_pinned(self):
        requirements = (quality.ROOT / "scripts/ci/requirements.txt").read_text()
        self.assertRegex(requirements, r"(?m)^ruff==\d+\.\d+\.\d+$")

    def test_ruff_has_an_explicit_low_noise_first_party_policy(self):
        import tomllib

        config = tomllib.loads((quality.ROOT / "ruff.toml").read_text())
        self.assertEqual(config["lint"]["select"], ["E4", "E7", "E9", "F"])
        self.assertEqual(config["extend-exclude"], ["build", "target"])


if __name__ == "__main__":
    unittest.main()
