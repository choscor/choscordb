#!/usr/bin/env python3
"""Build the pinned QScintilla Qt 6 shared library in an isolated prefix."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import urllib.request

VERSION = "2.14.1"
URL = "https://www.riverbankcomputing.com/static/Downloads/QScintilla/2.14.1/QScintilla_src-2.14.1.tar.gz"
SHA256 = "dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d"


def run(command, **kwargs):
    print(
        "Running:", subprocess.list2cmdline([str(arg) for arg in command]), flush=True
    )
    return subprocess.run(command, check=True, **kwargs)


def verify_archive(path):
    if hashlib.sha256(path.read_bytes()).hexdigest() != SHA256:
        raise ValueError(
            "QScintilla source SHA-256 mismatch; refusing to extract or execute it"
        )


def extract_archive(archive, destination):
    # Verify before extraction; reject links/devices and traversal even in pinned inputs.
    verify_archive(archive)
    with tarfile.open(archive) as source:
        for member in source.getmembers():
            target = (destination / member.name).resolve()
            if not target.is_relative_to(destination.resolve()) or not (
                member.isfile() or member.isdir()
            ):
                raise ValueError(f"Unsafe source archive member: {member.name!r}")
        source.extractall(destination, filter="data")


def macos_qmake_overrides(sdk):
    """Remove Qt's obsolete AGL link flags when the selected SDK no longer ships AGL."""
    agl = Path(sdk) / "System/Library/Frameworks/AGL.framework"
    return [] if agl.exists() else ["QMAKE_LIBS_OPENGL="]


def remove_missing_agl_from_makefile(makefile):
    """Remove obsolete AGL inherited from Qt framework PRL metadata."""
    text = makefile.read_text(encoding="utf-8")
    updated = re.sub(r" -framework AGL(?=\s|$)", "", text)
    if updated != text:
        makefile.write_text(updated, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmake", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument(
        "--archive",
        type=Path,
        help="Use a previously downloaded archive; SHA-256 is still enforced",
    )
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    qmake, prefix, work = (
        args.qmake.resolve(),
        args.prefix.resolve(),
        args.work.resolve(),
    )
    version = subprocess.check_output(
        [str(qmake), "-query", "QT_VERSION"], text=True
    ).strip()
    if tuple(map(int, version.split(".")[:2])) < (6, 8):
        raise ValueError("Qt 6.8 or newer is required")
    work.mkdir(parents=True, exist_ok=True)
    prefix.mkdir(parents=True, exist_ok=True)
    archive = (
        args.archive.resolve()
        if args.archive
        else work / f"QScintilla_src-{VERSION}.tar.gz"
    )
    if not archive.exists():
        temporary = archive.with_suffix(".download")
        with (
            urllib.request.urlopen(URL, timeout=120) as response,
            temporary.open("wb") as output,
        ):
            shutil.copyfileobj(response, output)
        verify_archive(temporary)
        temporary.replace(archive)
    extract_archive(archive, work)
    source = work / f"QScintilla_src-{VERSION}"
    build = work / "compiled"
    build.mkdir(exist_ok=True)
    library = prefix / "lib"
    library.mkdir(exist_ok=True)
    command = [
        str(qmake),
        str(source / "src/qscintilla.pro"),
        "CONFIG+=release",
        "CONFIG-=debug debug_and_release",
    ]
    without_agl = False
    if platform.system() == "Darwin":
        command.append(f"QMAKE_APPLE_DEVICE_ARCHS={platform.machine()}")
        sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
        overrides = macos_qmake_overrides(sdk)
        command.extend(overrides)
        without_agl = bool(overrides)
        if os.environ.get("MACOSX_DEPLOYMENT_TARGET"):
            command.append(
                f"QMAKE_MACOSX_DEPLOYMENT_TARGET={os.environ['MACOSX_DEPLOYMENT_TARGET']}"
            )
    run(command, cwd=build)
    if without_agl:
        remove_missing_agl_from_makefile(build / "Makefile")
    if os.name == "nt":
        # nmake comes from the MSVC developer environment; avoid GNU link.exe.
        run(["nmake", "/NOLOGO"], cwd=build)
    else:
        run(["make", f"-j{args.jobs}"], cwd=build)
    # Keep qmake's default output directory: upstream's macOS post-link command
    # addresses the library by basename and breaks when DESTDIR is overridden.
    for artifact in build.rglob("*qscintilla*"):
        if artifact.is_file() and (
            artifact.suffix in {".dylib", ".dll", ".lib"} or ".so" in artifact.name
        ):
            destination = library / artifact.name
            destination.unlink(missing_ok=True)
            shutil.copy2(artifact, destination, follow_symlinks=False)
    shutil.copytree(source / "src/Qsci", prefix / "include/Qsci", dirs_exist_ok=True)
    binary = prefix / "bin"
    binary.mkdir(exist_ok=True)
    for dll in library.glob("*.dll"):
        shutil.copy2(dll, binary / dll.name)
    notices = prefix / "share/licenses/QScintilla"
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "LICENSE", notices / "LICENSE")
    (notices / "source.json").write_text(
        json.dumps(
            {
                "version": VERSION,
                "url": URL,
                "sha256": SHA256,
                "license": "GPL-3.0-only",
                "qt_version": version,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    if not any(library.glob("*qscintilla*")):
        raise RuntimeError("QScintilla build produced no library")
    print(f"QScintilla installed into {prefix}")


if __name__ == "__main__":
    main()
