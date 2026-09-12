#!/usr/bin/env python3
"""Prepare and verify an unpublished, unsigned relocatable application stage."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


def run(argv, **kwargs):
    return subprocess.run(
        list(map(str, argv)),
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=120,
        **kwargs,
    ).stdout


def raise_walk_error(error):
    raise error


def require_release_configuration(build):
    cache = build / "CMakeCache.txt"
    if cache.is_symlink() or not cache.is_file():
        raise ValueError("CMake Release configuration evidence is missing")
    values = {}
    for line in cache.read_text(encoding="utf-8", errors="strict").splitlines():
        if not line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CONFIGURATION_TYPES:")):
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    build_type = values.get("CMAKE_BUILD_TYPE")
    configurations = values.get("CMAKE_CONFIGURATION_TYPES", "").split(";")
    if build_type != "Release" and "Release" not in configurations:
        raise ValueError("A CMake Release configuration is required for staging")


def regular_files(root):
    root = root.resolve()
    result = []
    for parent, directories, names in os.walk(
        root, followlinks=False, onerror=raise_walk_error
    ):
        for name in directories + names:
            path = Path(parent) / name
            if path.is_symlink():
                if path.readlink().is_absolute() or not path.resolve(
                    strict=True
                ).is_relative_to(root):
                    raise ValueError(f"Unsafe staged symlink: {path.relative_to(root)}")
                continue
            if path.is_file():
                relative = path.relative_to(root).as_posix()
                if (
                    "\\" in relative
                    or ":" in relative
                    or any(ord(c) < 32 for c in relative)
                ):
                    raise ValueError("Unsafe staged path")
                result.append(path)
            elif not path.is_dir():
                raise ValueError(f"Non-regular staged entry: {path}")
    return sorted(result, key=lambda path: path.relative_to(root).as_posix())


def is_macho(path):
    with path.open("rb") as source:
        return source.read(4) in {
            b"\xfe\xed\xfa\xce",
            b"\xce\xfa\xed\xfe",
            b"\xfe\xed\xfa\xcf",
            b"\xcf\xfa\xed\xfe",
            b"\xca\xfe\xba\xbe",
            b"\xbe\xba\xfe\xca",
            b"\xca\xfe\xba\xbf",
            b"\xbf\xba\xfe\xca",
        }


def dependencies(binary):
    return [
        line.strip().split(" (", 1)[0]
        for line in run(["otool", "-L", binary]).splitlines()
        if line.startswith("\t")
    ]


def rpaths(binary):
    paths, pending = [], False
    for line in run(["otool", "-l", binary]).splitlines():
        if line.strip() == "cmd LC_RPATH":
            pending = True
        elif pending and line.strip().startswith("path "):
            paths.append(line.strip()[5:].split(" (offset ", 1)[0])
            pending = False
    return paths


class MacOSAdapter:
    name = "macos"

    def layout(self, prefix):
        bundles = list(prefix.glob("*.app"))
        if len(bundles) != 1:
            raise ValueError("Expected exactly one installed application bundle")
        app = bundles[0]
        executable = app / "Contents/MacOS/choscordb"
        if (
            not executable.is_file()
            or not os.access(executable, os.X_OK)
            or not is_macho(executable)
        ):
            raise ValueError("Installed Mach-O executable is missing or not executable")
        return app, executable

    def deploy(self, prefix, qt_bin, qsci):
        app, executable = self.layout(prefix)
        framework = app / "Contents/Frameworks"
        framework.mkdir(parents=True, exist_ok=True)
        libraries = sorted((qsci / "lib").glob("*qscintilla*.dylib"))
        if not libraries:
            raise ValueError("QScintilla runtime is missing")
        for library in libraries:
            if not library.resolve(strict=True).is_relative_to(qsci):
                raise ValueError("QScintilla library escapes its prefix")
            # Materialize aliases to avoid copying absolute development symlinks.
            shutil.copy2(library.resolve(), framework / library.name)
        notices = app / "Contents/Resources/licenses"
        if not (notices / "LICENSE").is_file():
            raise ValueError("Installed ChoscorDB license is missing")
        if not (notices / "LICENSE-LUCIDE").is_file():
            raise ValueError("Installed Lucide icon license is missing")
        if not (notices / "SOURCE-LUCIDE.json").is_file():
            raise ValueError("Installed Lucide icon provenance is missing")
        icons = app / "Contents/Resources/icons"
        for name in ["app-mark.svg", "play.svg", "square.svg", "plus.svg"]:
            if not (icons / name).is_file():
                raise ValueError(f"Installed application icon missing: {name}")
        source_notices = qsci / "share/licenses/QScintilla"
        if not source_notices.resolve().is_relative_to(qsci):
            raise ValueError("QScintilla notices escape their prefix")
        for name in ["LICENSE", "source.json"]:
            if not (source_notices / name).is_file():
                raise ValueError(f"QScintilla notice missing: {name}")
        regular_files(source_notices)
        shutil.copytree(source_notices, notices / "QScintilla")
        qt_notices = qt_bin.parent / "licenses"
        if qt_notices.is_dir():
            regular_files(qt_notices)
            shutil.copytree(qt_notices, notices / "Qt")
        for binary in [executable, *sorted(framework.glob("*.dylib"))]:
            for dependency in dependencies(binary):
                if "qscintilla" in Path(dependency).name.lower():
                    replacement = "@rpath/" + Path(dependency).name
                    if not (framework / Path(dependency).name).is_file():
                        raise ValueError("Referenced QScintilla library was not copied")
                    if dependency != replacement:
                        run(
                            [
                                "install_name_tool",
                                "-change",
                                dependency,
                                replacement,
                                binary,
                            ]
                        )
            if binary != executable:
                run(["install_name_tool", "-id", "@rpath/" + binary.name, binary])
        if "@executable_path/../Frameworks" not in rpaths(executable):
            run(
                [
                    "install_name_tool",
                    "-add_rpath",
                    "@executable_path/../Frameworks",
                    executable,
                ]
            )
        tool = qt_bin / "macdeployqt"
        if not tool.is_file():
            raise ValueError("macdeployqt is missing")
        run([tool, app, "-always-overwrite", "-verbose=1"])

    def validate(self, prefix):
        prefix = prefix.resolve()
        app, executable = self.layout(prefix)
        plugin = app / "Contents/PlugIns/platforms/libqcocoa.dylib"
        if not plugin.is_file() or not is_macho(plugin):
            raise ValueError("Cocoa platform plugin is missing")
        files = regular_files(prefix)
        main_rpaths = rpaths(executable)

        def expand(path, binary):
            if path.startswith("@loader_path/"):
                return binary.parent / path[len("@loader_path/") :]
            if path.startswith("@executable_path/"):
                return executable.parent / path[len("@executable_path/") :]
            if path.startswith("/"):
                return Path(path)
            raise ValueError(f"Unsupported Mach-O location: {path}")

        for binary in files:
            if not is_macho(binary):
                continue
            search = rpaths(binary)
            for path in search:
                if path.startswith("/"):
                    raise ValueError(f"Non-relocatable rpath in {binary.name}: {path}")
                resolved = expand(path, binary).resolve()
                if not resolved.is_relative_to(prefix):
                    raise ValueError(f"Non-relocatable rpath in {binary.name}: {path}")
            for dependency in dependencies(binary):
                if dependency.startswith(("/usr/lib/", "/System/Library/")):
                    continue  # Apple shared-cache libraries need not exist as individual files.
                if dependency.startswith("@rpath/"):
                    tail = dependency[len("@rpath/") :]
                    candidates = [expand(path, binary) / tail for path in search]
                    candidates += [
                        expand(path, executable) / tail for path in main_rpaths
                    ]
                else:
                    if dependency.startswith("/"):
                        raise ValueError(
                            f"Absolute non-system dependency: {dependency}"
                        )
                    candidates = [expand(dependency, binary)]
                if not any(
                    path.is_file()
                    and path.resolve().is_relative_to(prefix)
                    and is_macho(path)
                    for path in candidates
                ):
                    raise ValueError(
                        f"Unresolved dependency in {binary.name}: {dependency}"
                    )
        return executable

    def smoke(self, executable, home):
        env = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith(("DYLD_", "QT_", "QML_", "QML2_"))
            and key not in {"LD_LIBRARY_PATH", "LD_PRELOAD"}
        }
        env.update(
            HOME=str(home),
            CFFIXED_USER_HOME=str(home),
            XDG_DATA_HOME=str(home / "data"),
            XDG_CONFIG_HOME=str(home / "config"),
            XDG_CACHE_HOME=str(home / "cache"),
            TMPDIR=str(home),
            PATH="/usr/bin:/bin:/usr/sbin:/sbin",
        )
        run([executable, "--smoke-test"], env=env)
        databases = [
            path
            for path in home.rglob("choscordb.sqlite")
            if path.is_file() and not path.is_symlink()
        ]
        if len(databases) != 1:
            raise ValueError(
                "Smoke test did not create exactly one temporary metadata database"
            )


def create_stage(build, output, qt_bin, qsci, source_manifest, adapter=None):
    build, qt_bin, qsci = (
        Path(path).resolve(strict=True) for path in (build, qt_bin, qsci)
    )
    source_manifest = Path(source_manifest).resolve(strict=True)
    if source_manifest.is_symlink() or not source_manifest.is_file():
        raise ValueError("Source-candidate manifest must be a regular file")
    source_bytes = source_manifest.read_bytes()
    source = json.loads(source_bytes)
    if (
        source.get("format_version") != 1
        or source.get("source_kind") != "working-tree snapshot"
        or not isinstance(source.get("files"), list)
        or source.get("git_revision") is not None
        and not isinstance(source.get("git_revision"), str)
    ):
        raise ValueError("Source-candidate manifest is invalid")
    require_release_configuration(build)
    output = Path(output).absolute()
    if os.path.lexists(output):
        raise FileExistsError(output)
    if adapter is None:
        if platform.system() != "Darwin":
            raise ValueError("Only the macOS staging adapter is implemented")
        adapter = MacOSAdapter()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".stage-", dir=output.parent)).resolve()
    published = False
    try:
        run(["cmake", "--install", build, "--config", "Release", "--prefix", temporary])
        regular_files(temporary)
        adapter.deploy(temporary, qt_bin, qsci)
        adapter.validate(temporary)
        manifest = {
            "format_version": 1,
            "platform": adapter.name,
            "status": "unsigned-stage",
            "source_candidate": {
                "manifest_sha256": hashlib.sha256(source_bytes).hexdigest(),
                "source_kind": source["source_kind"],
                "git_revision": source.get("git_revision"),
            },
            "limitations": [
                "Not an installer or signed/notarized release.",
                "Available notices only; dependency notice completeness and SBOM remain unverified.",
            ],
            "files": [],
        }
        for path in regular_files(temporary):
            data = path.read_bytes()
            manifest["files"].append(
                {
                    "path": path.relative_to(temporary).as_posix(),
                    "size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest(),
                    "executable": bool(path.stat().st_mode & 0o111),
                }
            )
        document = json.dumps(manifest, indent=2, sort_keys=True).encode() + b"\n"
        (temporary / "manifest.json").write_bytes(document)
        (temporary / "SHA256SUMS").write_text(
            hashlib.sha256(document).hexdigest() + "  manifest.json\n"
        )
        if os.path.lexists(output):
            raise FileExistsError(output)
        temporary.rename(output)
        published = True
        executable = adapter.validate(output)
        with tempfile.TemporaryDirectory(prefix="choscordb-stage-home-") as home:
            adapter.smoke(executable, Path(home))
    except BaseException:
        cleanup = output if published else temporary
        shutil.rmtree(cleanup)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in [
        "build",
        "output",
        "qt-bin",
        "qscintilla-prefix",
        "source-candidate-manifest",
    ]:
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    print(
        create_stage(
            args.build,
            args.output,
            args.qt_bin,
            args.qscintilla_prefix,
            args.source_candidate_manifest,
        )
    )


if __name__ == "__main__":
    main()
