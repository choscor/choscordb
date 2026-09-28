#!/usr/bin/env python3
"""Build verified Windows ZIP and Linux AppImage release candidates.

This tool prepares local artifacts. It never signs, tags, or uploads a release.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib
import urllib.request
import zipfile

import cargo_licenses
from prepare_qt_notices import SHA256 as QTBASE_SHA256, SVG_SHA256 as QTSVG_SHA256
from stage import require_release_configuration

ROOT = Path(__file__).resolve().parents[2]
LINUX_TOOLS = {
    "linuxdeploy-x86_64.AppImage": (
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage",
        "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d",
    ),
    "linuxdeploy-plugin-qt-x86_64.AppImage": (
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage",
        "15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724",
    ),
}


def run(command, **kwargs):
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def version():
    value = tomllib.loads((ROOT / "Cargo.toml").read_text())["workspace"]["package"][
        "version"
    ]
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value):
        raise ValueError("A stable Cargo workspace version is required")
    return value


def require_clean_source():
    status = subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=normal"],
        cwd=ROOT,
        text=True,
    )
    if status.strip():
        raise ValueError("Release candidates require a clean source checkout")


def require_inputs(build, qt, qsci, output):
    require_clean_source()
    require_release_configuration(build)
    cache = (build / "CMakeCache.txt").read_text()
    if f"CMAKE_PROJECT_VERSION:STATIC={version()}" not in cache.splitlines():
        raise ValueError("CMake build version differs from Cargo release version")
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    qmake = qt / "bin" / ("qmake.exe" if os.name == "nt" else "qmake")
    if not qmake.is_file():
        raise ValueError("Qt installation is missing qmake")
    qt_version = subprocess.check_output(
        [qmake, "-query", "QT_VERSION"], text=True
    ).strip()
    if qt_version != "6.8.3":
        raise ValueError("Qt runtime must be 6.8.3")
    if not (qsci / "share/licenses/QScintilla/LICENSE").is_file():
        raise ValueError("QScintilla license is missing")
    if not (qsci / "share/licenses/QScintilla/source.json").is_file():
        raise ValueError("QScintilla source provenance is missing")
    qsci_source = json.loads(
        (qsci / "share/licenses/QScintilla/source.json").read_text()
    )
    if (
        qsci_source.get("version") != "2.14.1"
        or qsci_source.get("qt_version") != qt_version
        or qsci_source.get("sha256")
        != "dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d"
    ):
        raise ValueError("QScintilla provenance differs from release pins")


def install(build, prefix):
    run(["cmake", "--install", build, "--prefix", prefix], cwd=ROOT)
    if not (prefix / "share/licenses/choscordb/LICENSE").is_file():
        raise ValueError("Installed application license is missing")


def add_notices(prefix, qsci, qt_notices, cargo_target):
    licenses = prefix / "share/licenses/choscordb"
    qt_source = Path(qt_notices)
    if not (qt_source / "source.json").is_file():
        raise ValueError("Verified Qt source notices are required")
    record = json.loads((qt_source / "source.json").read_text())
    if record.get("version") != "6.8.3" or set(record.get("modules", {})) != {
        "qtbase",
        "qtsvg",
    }:
        raise ValueError("Qt notice modules or version do not match the pinned runtime")
    if (
        record["modules"]["qtbase"].get("sha256") != QTBASE_SHA256
        or record["modules"]["qtsvg"].get("sha256") != QTSVG_SHA256
    ):
        raise ValueError("Qt notice source archives differ from release pins")
    expected = {
        entry["path"]: entry["sha256"]
        for module in record["modules"].values()
        for entry in module["copied_files"]
    }
    actual = {
        path.relative_to(qt_source).as_posix(): sha256(path)
        for path in qt_source.rglob("*")
        if path.is_file() and path.name != "source.json"
    }
    if expected != actual:
        raise ValueError("Qt source notice hashes or file set mismatch")
    shutil.copytree(qt_source, licenses / "Qt")
    shutil.copytree(qsci / "share/licenses/QScintilla", licenses / "QScintilla")
    metadata = licenses / "cargo-metadata.json"
    with metadata.open("w") as stream:
        run(
            [
                "cargo",
                "metadata",
                "--locked",
                "--format-version",
                "1",
                "--filter-platform",
                cargo_target,
            ],
            cwd=ROOT,
            stdout=stream,
        )
    cargo_licenses.collect(
        metadata,
        ROOT / "docs/licenses/cargo-fallbacks",
        licenses / "Cargo",
        "choscordb-bridge",
    )
    metadata.unlink()
    packages = json.loads((licenses / "Cargo/index.json").read_text())["packages"]
    lines = [
        "# Third-party notices",
        "",
        "Qt 6.8.3: see Qt/ for source license texts and provenance.",
        "QScintilla 2.14.1: GPL-3.0-only; see QScintilla/.",
        "",
        "## Rust dependencies",
        "",
    ]
    for package in packages:
        lines.append(
            f"- {package['name']} {package['version']} — {package['license'] or 'NOASSERTION'}; "
            f"license texts: Cargo/{package['name']}@{package['version']}/"
        )
    (licenses / "THIRD_PARTY_NOTICES.md").write_text("\n".join(lines) + "\n")


def write_inventory(root, path):
    files = []
    for item in sorted(root.rglob("*")):
        if item.is_symlink():
            if item.readlink().is_absolute():
                raise ValueError(f"Absolute package symlink: {item}")
            target = item.resolve(strict=True)
            if not target.is_relative_to(root.resolve()):
                raise ValueError(f"Package symlink escapes stage: {item}")
            files.append(
                {
                    "path": item.relative_to(root).as_posix(),
                    "symlink": str(item.readlink()),
                }
            )
            continue
        if item.is_file():
            files.append(
                {
                    "path": item.relative_to(root).as_posix(),
                    "size": item.stat().st_size,
                    "sha256": sha256(item),
                }
            )
    path.write_text(json.dumps(files, indent=2, sort_keys=True) + "\n")


def package_windows(build, qt, qsci, qt_notices, output):
    require_inputs(build, qt, qsci, output)
    output.mkdir(parents=True)
    name = f"ChoscorDB-{version()}-windows-x64"
    with tempfile.TemporaryDirectory(prefix="choscordb-win-") as temporary:
        stage = Path(temporary) / name
        install(build, stage)
        exe = stage / "bin/choscordb.exe"
        if not exe.is_file():
            raise ValueError("Release executable is missing")
        shutil.move(exe, stage / exe.name)
        (stage / "bin").rmdir()
        deploy = qt / "bin/windeployqt.exe"
        if not deploy.is_file():
            raise ValueError("windeployqt.exe is missing")
        dlls = sorted((qsci / "bin").glob("*qscintilla*.dll"))
        if not dlls:
            raise ValueError("QScintilla runtime DLL is missing")
        for dll in dlls:
            shutil.copy2(dll, stage / dll.name)
        print_support = qt / "bin/Qt6PrintSupport.dll"
        if not print_support.is_file():
            raise ValueError("QScintilla Qt PrintSupport runtime DLL is missing")
        shutil.copy2(print_support, stage / print_support.name)
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join(
            [str(qt / "bin"), str(qsci / "bin"), env.get("PATH", "")]
        )
        run([deploy, "--release", "--dir", stage, stage / exe.name], env=env)
        add_notices(stage, qsci, qt_notices, "x86_64-pc-windows-msvc")
        if not (stage / "platforms/qwindows.dll").is_file():
            raise ValueError("Windows Qt platform plugin was not deployed")
        smoke_env = os.environ.copy()
        smoke_env.pop("QT_PLUGIN_PATH", None)
        smoke_env["QT_QPA_PLATFORM"] = "windows"
        smoke_env["APPDATA"] = str(Path(temporary) / "appdata")
        smoke_env["LOCALAPPDATA"] = str(Path(temporary) / "localappdata")
        run([stage / exe.name, "--smoke-test"], env=smoke_env, timeout=60)
        asset = output / f"{name}.zip"
        with zipfile.ZipFile(
            asset, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for item in sorted(stage.rglob("*")):
                if item.is_file():
                    archive.write(item, f"{name}/{item.relative_to(stage).as_posix()}")
        with zipfile.ZipFile(asset) as archive:
            if archive.testzip() is not None:
                raise ValueError("Windows ZIP failed integrity verification")
            for required in (
                f"{name}/choscordb.exe",
                f"{name}/Qt6PrintSupport.dll",
                f"{name}/share/licenses/choscordb/LICENSE",
                f"{name}/share/licenses/choscordb/Qt/source.json",
                f"{name}/share/licenses/choscordb/QScintilla/LICENSE",
                f"{name}/share/licenses/choscordb/Cargo/index.json",
            ):
                if required not in archive.namelist():
                    raise ValueError(f"Windows ZIP is missing {required}")
        write_inventory(stage, output / f"{name}-inventory.json")
    write_asset_digest(asset)
    return asset


def download_linux_tools(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for name, (url, digest) in LINUX_TOOLS.items():
        target = directory / name
        if target.exists() and sha256(target) == digest:
            continue
        temporary = target.with_suffix(".download")
        with (
            urllib.request.urlopen(url, timeout=120) as response,
            temporary.open("wb") as stream,
        ):
            shutil.copyfileobj(response, stream)
        if sha256(temporary) != digest:
            temporary.unlink(missing_ok=True)
            raise ValueError(f"Pinned {name} digest mismatch")
        temporary.replace(target)
        target.chmod(0o755)
    return directory / "linuxdeploy-x86_64.AppImage"


def package_linux(build, qt, qsci, qt_notices, output):
    require_inputs(build, qt, qsci, output)
    output.mkdir(parents=True)
    tool = download_linux_tools(output / "tools")
    name = f"ChoscorDB-{version()}-linux-x86_64"
    with tempfile.TemporaryDirectory(prefix="choscordb-linux-") as temporary:
        work = Path(temporary)
        appdir = work / "AppDir"
        install(build, appdir / "usr")
        executable = appdir / "usr/bin/choscordb"
        if not executable.is_file():
            raise ValueError("Release executable is missing")
        add_notices(appdir / "usr", qsci, qt_notices, "x86_64-unknown-linux-gnu")
        desktop = appdir / "usr/share/applications/choscordb.desktop"
        desktop.parent.mkdir(parents=True)
        desktop.write_text(
            "[Desktop Entry]\nType=Application\nName=ChoscorDB\nExec=choscordb\n"
            "Icon=choscordb\nCategories=Development;Database;\nTerminal=false\n"
        )
        icon = appdir / "usr/share/icons/hicolor/256x256/apps/choscordb.png"
        icon.parent.mkdir(parents=True)
        import cairosvg

        cairosvg.svg2png(
            url=str(ROOT / "desktop/resources/icons/app-mark.svg"),
            write_to=str(icon),
            output_width=256,
            output_height=256,
        )
        libraries = sorted((qsci / "lib").glob("*qscintilla*.so*"))
        libraries = [
            item for item in libraries if item.is_file() and not item.is_symlink()
        ]
        if not libraries:
            raise ValueError("QScintilla shared library is missing")
        env = os.environ.copy()
        env["PATH"] = os.pathsep.join(
            [str(tool.parent), str(qt / "bin"), env.get("PATH", "")]
        )
        env["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(qsci / "lib"), str(qt / "lib"), env.get("LD_LIBRARY_PATH", "")]
        )
        env["QMAKE"] = str(qt / "bin/qmake")
        env["EXTRA_PLATFORM_PLUGINS"] = "libqoffscreen.so"
        env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
        run(
            [
                tool,
                "--appdir",
                appdir,
                "--executable",
                executable,
                "--desktop-file",
                desktop,
                "--icon-file",
                icon,
                "--library",
                libraries[0],
                "--plugin",
                "qt",
                "--output",
                "appimage",
            ],
            cwd=work,
            env=env,
            timeout=600,
        )
        produced = list(work.glob("*.AppImage"))
        if len(produced) != 1:
            raise ValueError("linuxdeploy did not produce exactly one AppImage")
        asset = output / f"{name}.AppImage"
        shutil.move(produced[0], asset)
        asset.chmod(0o755)
        smoke_env = os.environ.copy()
        for key in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QMAKE"):
            smoke_env.pop(key, None)
        smoke_env["QT_QPA_PLATFORM"] = "offscreen"
        smoke_env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
        smoke_env["XDG_DATA_HOME"] = str(work / "smoke-data")
        run([asset, "--smoke-test"], env=smoke_env, timeout=120)
        extract_env = smoke_env.copy()
        extract_env.pop("APPIMAGE_EXTRACT_AND_RUN")
        run(
            [asset, "--appimage-extract"],
            cwd=work,
            env=extract_env,
            stdout=subprocess.DEVNULL,
            timeout=120,
        )
        extracted = work / "squashfs-root/usr/share/licenses/choscordb"
        for required in (
            "LICENSE",
            "Qt/source.json",
            "QScintilla/LICENSE",
            "Cargo/index.json",
        ):
            if not (extracted / required).is_file():
                raise ValueError(f"AppImage is missing {required}")
        write_inventory(appdir, output / f"{name}-inventory.json")
    write_asset_digest(asset)
    shutil.rmtree(output / "tools")
    return asset


def write_asset_digest(asset):
    (asset.parent / f"{asset.name}.sha256").write_text(
        f"{sha256(asset)}  {asset.name}\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("windows", "linux"))
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--qt", required=True, type=Path)
    parser.add_argument("--qscintilla", required=True, type=Path)
    parser.add_argument("--qt-notices", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        function = package_windows if args.platform == "windows" else package_linux
        print(
            function(
                args.build.resolve(),
                args.qt.resolve(),
                args.qscintilla.resolve(),
                args.qt_notices.resolve(),
                args.output.resolve(),
            )
        )
    except (
        ValueError,
        FileExistsError,
        OSError,
        subprocess.CalledProcessError,
    ) as error:
        parser.exit(1, f"Cross-platform packaging failed: {error}\n")


if __name__ == "__main__":
    main()
