#!/usr/bin/env python3
"""Run ChoscorDB's canonical local quality gates."""

import argparse
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
PYTHON = sys.executable
CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".mm", ".h", ".hh", ".hpp", ".hxx"}
QT_VERSION = "6.8.3"


class QualityError(RuntimeError):
    """A quality stage cannot start because its prerequisites are unavailable."""


def run_command(command, **kwargs):
    """Print and run a quality command from the repository root."""
    rendered = subprocess.list2cmdline([str(argument) for argument in command])
    print(f"Running: {rendered}", flush=True)
    subprocess.run(command, check=True, cwd=ROOT, **kwargs)


def require_tool(tool, *, install):
    """Require an executable and provide stage-specific installation guidance."""
    if shutil.which(tool) is None:
        raise QualityError(
            f"Required tool '{tool}' was not found. Install it with: {install}"
        )
    return tool


def require_pinned_tool(tool, version_args, expected, *, install):
    """Require a tool and reject a version outside the repository pin."""
    executable = require_tool(tool, install=install)
    command = [executable, *version_args]
    print(f"Running: {subprocess.list2cmdline(command)}", flush=True)
    completed = subprocess.run(
        command,
        check=True,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    reported = completed.stdout.strip()
    versions = re.findall(r"(?<![\d.])v?(\d+\.\d+\.\d+)(?![\d.])", reported)
    if not versions or versions[0] != expected:
        raise QualityError(
            f"{tool} must be version {expected}, but reported: {reported or '<no version>'}. "
            f"Install it with: {install}"
        )
    return executable


def clang_format_tool():
    """Locate clang-format and verify the repository's pinned LLVM major."""
    executable = next(
        (
            candidate
            for candidate in ("clang-format-23", "clang-format")
            if shutil.which(candidate)
        ),
        None,
    )
    if executable is None:
        raise QualityError(
            "Required tool 'clang-format' was not found. Install LLVM 23 and put "
            "clang-format-23 (or its clang-format) on PATH."
        )
    command = [executable, "--version"]
    print(f"Running: {subprocess.list2cmdline(command)}", flush=True)
    completed = subprocess.run(
        command,
        check=True,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    version = completed.stdout.strip()
    if re.search(r"\bversion\s+23(?:\.|\b)", version) is None:
        raise QualityError(
            f"C++ formatting requires LLVM 23, but '{executable} --version' reported: "
            f"{version or '<no version>'}"
        )
    return executable


def cpp_files():
    """Return the deterministic first-party hand-written C/C++ formatting scope."""
    files = []
    for directory in (ROOT / "desktop", ROOT / "tests"):
        files.extend(
            path.relative_to(ROOT)
            for path in directory.rglob("*")
            if path.is_file() and path.suffix in CPP_SUFFIXES
        )
    return sorted(files)


def cpp_format():
    run_command([clang_format_tool(), "--dry-run", "--Werror", *cpp_files()])


def cpp_size():
    run_command([PYTHON, "scripts/ci/cpp_size.py"])


def qss_lint():
    run_command([PYTHON, "scripts/ci/qss_policy.py"])


def ui_policy():
    run_command([PYTHON, "scripts/ci/ui_policy.py"])


def ui_consistency():
    run_command([PYTHON, "scripts/ci/ui_consistency.py"])


def python_lint():
    ruff = require_tool(
        "ruff", install=f"{PYTHON} -m pip install -r scripts/ci/requirements.txt"
    )
    run_command([ruff, "check", "scripts", "examples"])


def python_format():
    ruff = require_tool(
        "ruff", install=f"{PYTHON} -m pip install -r scripts/ci/requirements.txt"
    )
    run_command([ruff, "format", "--check", "scripts", "examples"])


def actionlint_tool():
    return require_pinned_tool(
        "actionlint",
        ["-version"],
        "1.7.7",
        install="go install github.com/rhysd/actionlint/cmd/actionlint@v1.7.7",
    )


def actionlint():
    run_command([actionlint_tool(), "-color"])


def python_tests():
    for directory in ("scripts/ci", "scripts/release"):
        run_command(
            [
                PYTHON,
                "-m",
                "unittest",
                "discover",
                "-s",
                directory,
                "-p",
                "test_*.py",
            ]
        )


def cargo_tool():
    return require_tool(
        "cargo",
        install="rustup toolchain install 1.97.1 --component rustfmt --component clippy",
    )


def rust_format():
    run_command([cargo_tool(), "fmt", "--all", "--", "--check"])


def rust_check():
    run_command(
        [
            cargo_tool(),
            "check",
            "--workspace",
            "--all-targets",
            "--all-features",
            "--locked",
        ]
    )


def rust_clippy():
    run_command(
        [
            cargo_tool(),
            "clippy",
            "--workspace",
            "--all-targets",
            "--all-features",
            "--locked",
            "--",
            "-D",
            "warnings",
        ]
    )


def rust_tests():
    run_command([cargo_tool(), "test", "--workspace", "--all-features", "--locked"])


def cargo_deny_tool():
    return require_pinned_tool(
        "cargo-deny",
        ["--version"],
        "0.20.2",
        install="cargo install cargo-deny --version 0.20.2 --locked",
    )


def cargo_deny():
    cargo_deny_tool()
    run_command([cargo_tool(), "deny", "--locked", "check"])


def desktop(stage):
    """Delegate native definitions to desktop.py, their single source of truth."""
    run_command([PYTHON, "scripts/ci/desktop.py", stage])


def qt_directory():
    return {"Windows": "msvc2022_64", "Darwin": "macos"}.get(
        platform.system(), "gcc_64"
    )


def qmake_name():
    return "qmake.exe" if platform.system() == "Windows" else "qmake"


def require_native_dependencies():
    """Fail before CMake with the command that provisions Qt and QScintilla."""
    tools = ROOT / "build/ci"
    qt = tools / "qt" / QT_VERSION / qt_directory()
    qsci = tools / "qscintilla"
    expected = {
        "Qt qmake": qt / "bin" / qmake_name(),
        "QScintilla header": qsci / "include/Qsci/qsciscintilla.h",
    }
    missing = [label for label, path in expected.items() if not path.is_file()]
    library_suffixes = {".a", ".so", ".dylib", ".lib", ".dll"}
    have_qsci_library = qsci.is_dir() and any(
        "qscintilla" in path.name.lower() and path.suffix.lower() in library_suffixes
        for path in qsci.rglob("*")
        if path.is_file()
    )
    if not have_qsci_library:
        missing.append("QScintilla library")
    if missing:
        raise QualityError(
            "Native dependencies are unavailable "
            f"({', '.join(missing)}). Provision them with: "
            f"{PYTHON} scripts/ci/quality.py native-dependencies"
        )


def native_build():
    require_native_dependencies()
    require_tool(
        "cmake", install=f"{PYTHON} -m pip install -r scripts/ci/requirements.txt"
    )
    require_tool(
        "ninja", install=f"{PYTHON} -m pip install -r scripts/ci/requirements.txt"
    )
    desktop("build")


def native_tests():
    require_native_dependencies()
    require_tool(
        "ctest", install=f"{PYTHON} -m pip install -r scripts/ci/requirements.txt"
    )
    desktop("test")


STAGES = {
    "cpp-format": cpp_format,
    "cpp-size": cpp_size,
    "qss-lint": qss_lint,
    "ui-policy": ui_policy,
    "ui-consistency": ui_consistency,
    "python-lint": python_lint,
    "python-format": python_format,
    "actionlint": actionlint,
    "python-tests": python_tests,
    "rust-format": rust_format,
    "rust-check": rust_check,
    "rust-clippy": rust_clippy,
    "rust-tests": rust_tests,
    "cargo-deny": cargo_deny,
    "native-dependencies": lambda: desktop("dependencies"),
    "native-build": native_build,
    "native-tests": native_tests,
}

FAST_STAGES = (
    "cpp-format",
    "cpp-size",
    "qss-lint",
    "ui-policy",
    "ui-consistency",
    "python-lint",
    "python-format",
    "actionlint",
    "python-tests",
    "rust-format",
    "rust-check",
    "rust-clippy",
    "rust-tests",
    "cargo-deny",
)
FULL_STAGES = (*FAST_STAGES, "native-build", "native-tests")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=["fast", "full", *STAGES])
    args = parser.parse_args(argv)
    selected = (
        FAST_STAGES
        if args.stage == "fast"
        else FULL_STAGES
        if args.stage == "full"
        else (args.stage,)
    )
    try:
        for stage in selected:
            print(f"\n== {stage} ==", flush=True)
            STAGES[stage]()
    except QualityError as error:
        print(f"quality: {error}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as error:
        return error.returncode or 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
