#!/usr/bin/env python3
"""Install pinned CI Qt dependencies or configure/build/test the native app."""

import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
QT_VERSION = "6.8.3"


def run(args, **kwargs):
    print("Running:", subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    subprocess.run(args, check=True, cwd=ROOT, **kwargs)


def configuration():
    system = platform.system()
    if system == "Windows":
        return "windows", "win64_msvc2022_64", "msvc2022_64"
    if system == "Darwin":
        return "mac", "clang_64", "macos"
    return "linux", "linux_gcc_64", "gcc_64"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", choices=["dependencies", "build", "test", "rust"])
    args = parser.parse_args()
    tools = ROOT / "build/ci"
    host, architecture, directory = configuration()
    qt = tools / "qt" / QT_VERSION / directory
    qsci = tools / "qscintilla"
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join(
        [str(qsci / "bin"), str(qt / "bin"), env.get("PATH", "")]
    )
    env["QT_PLUGIN_PATH"] = str(qt / "plugins")
    env["QT_QPA_PLATFORM"] = "offscreen"
    if host == "linux":
        env["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(qsci / "lib"), str(qt / "lib"), env.get("LD_LIBRARY_PATH", "")]
        )
    if host == "mac":
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(
            [str(qsci / "lib"), str(qt / "lib"), env.get("DYLD_LIBRARY_PATH", "")]
        )
        env["DYLD_FRAMEWORK_PATH"] = str(qt / "lib")
    if args.stage == "dependencies":
        run(
            [
                sys.executable,
                "-m",
                "aqt",
                "install-qt",
                host,
                "desktop",
                QT_VERSION,
                architecture,
                "--outputdir",
                str(tools / "qt"),
            ]
        )
        qmake = qt / "bin" / ("qmake.exe" if host == "windows" else "qmake")
        run(
            [
                sys.executable,
                "scripts/ci/bootstrap_qscintilla.py",
                "--qmake",
                str(qmake),
                "--prefix",
                str(qsci),
                "--work",
                str(tools / "qscintilla-source"),
                "--jobs",
                "2",
            ],
            env=env,
        )
    elif args.stage == "build":
        run(
            [
                "cmake",
                "-S",
                ".",
                "-B",
                "build/ci/native",
                "-G",
                "Ninja",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_TESTING=ON",
                f"-DCMAKE_PREFIX_PATH={qt};{qsci}",
            ],
            env=env,
        )
        run(["cmake", "--build", "build/ci/native", "--parallel", "2"], env=env)
    elif args.stage == "test":
        command = [
            "ctest",
            "--test-dir",
            "build/ci/native",
            "--output-on-failure",
            "--timeout",
            "120",
        ]
        if platform.system() in {"Darwin", "Windows"}:
            command.append("--verbose")
        run(
            command,
            env=env,
        )
    else:
        toolchain = tomllib.loads((ROOT / "rust-toolchain.toml").read_text())[
            "toolchain"
        ]["channel"]
        run(
            [
                "rustup",
                "toolchain",
                "install",
                toolchain,
                "--profile",
                "minimal",
                "--component",
                "rustfmt",
                "--component",
                "clippy",
            ]
        )
        run(["cargo", "fmt", "--all", "--", "--check"])
        run(
            [
                "cargo",
                "clippy",
                "--workspace",
                "--all-targets",
                "--locked",
                "--",
                "-D",
                "warnings",
            ]
        )
        run(["cargo", "test", "--workspace", "--locked"])


if __name__ == "__main__":
    main()
