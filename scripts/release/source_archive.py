#!/usr/bin/env python3
"""Prepare a deterministic, unpublished working-tree source candidate."""

import argparse
from contextlib import ExitStack
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tarfile

SOURCE_ROOTS = (".github", "cmake", "crates", "desktop", "docs", "scripts", "tests")
ROOT_FILES = (
    ".clang-format",
    ".gitignore",
    "CMakeLists.txt",
    "CMakePresets.json",
    "Cargo.lock",
    "Cargo.toml",
    "CHANGELOG.md",
    "LICENSE",
    "README.md",
    "deny.toml",
    "rust-toolchain.toml",
)
EXCLUDED_DIRECTORIES = {
    "build",
    "target",
    ".git",
    "__pycache__",
    ".venv",
    "venv",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    ".cache",
    "node_modules",
    "CMakeFiles",
}
EXCLUDED_SUFFIXES = (
    ".png",
    ".log",
    ".key",
    ".pem",
    ".p12",
    ".pfx",
    ".pyc",
    ".pyo",
    ".sqlite",
    ".sqlite3",
    ".db",
    ".sqlite-wal",
    ".sqlite-shm",
    ".sqlite-journal",
    ".sqlite3-wal",
    ".sqlite3-shm",
    ".sqlite3-journal",
    ".db-wal",
    ".db-shm",
    ".db-journal",
    ".swp",
    ".swo",
    ".bak",
    ".orig",
    ".rej",
    "~",
    ".o",
    ".obj",
    ".a",
    ".lib",
    ".so",
    ".dylib",
    ".dll",
    ".exe",
    ".pdb",
)


def excluded_file(name):
    lower = name.lower()
    return (
        lower.endswith(EXCLUDED_SUFFIXES)
        or lower
        in {
            ".ds_store",
            ".env",
            "cmakecache.txt",
            "cmake_install.cmake",
            "compile_commands.json",
            "install_manifest.txt",
        }
        or ".so." in lower
        or lower.startswith(".env.")
        or (name.startswith("#") and name.endswith("#"))
    )


def raise_walk_error(error):
    raise error


def read_source(path):
    """Traverse every component through pinned directory handles, never symlinks.

    Platforms without descriptor-relative no-follow opens fail closed rather than
    using a check-then-open fallback with an ancestor replacement race.
    """
    if os.open not in os.supports_dir_fd or not hasattr(os, "O_NOFOLLOW"):
        raise ValueError(
            "Secure source traversal requires descriptor-relative no-follow opens"
        )
    with ExitStack() as handles:
        directory_flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        directory = os.open(path.anchor, directory_flags)
        handles.callback(os.close, directory)
        for component in path.parts[1:-1]:
            directory = os.open(component, directory_flags, dir_fd=directory)
            handles.callback(os.close, directory)
        descriptor = os.open(
            path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory
        )
        handles.callback(os.close, descriptor)
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError(f"Non-regular source file: {path}")
        with os.fdopen(os.dup(descriptor), "rb") as source:
            return source.read(), metadata.st_mode


def source_files(root):
    files = []
    for name in ROOT_FILES:
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Required regular root file missing or unsafe: {name}")
        files.append(path)
    for name in SOURCE_ROOTS:
        tree = root / name
        if tree.is_symlink() or not tree.is_dir():
            raise ValueError(f"Required source directory missing or unsafe: {name}")
        for directory, directories, names in os.walk(
            tree, followlinks=False, onerror=raise_walk_error
        ):
            parent = Path(directory)
            for child in directories + names:
                if (parent / child).is_symlink():
                    raise ValueError(
                        f"Symlink in source tree: {(parent / child).relative_to(root)}"
                    )
            directories[:] = sorted(
                d
                for d in directories
                if d not in EXCLUDED_DIRECTORIES
                and not d.lower().endswith(".dsym")
                and not d.lower().startswith("cmake-build-")
            )
            for child in names:
                path = parent / child
                build_artwork = name == "desktop" and child.lower().endswith(".png")
                if excluded_file(child) and not build_artwork:
                    continue
                if not stat.S_ISREG(path.stat().st_mode):
                    raise ValueError(
                        f"Non-regular source file: {path.relative_to(root)}"
                    )
                files.append(path)
    for path in files:
        relative = path.relative_to(root).as_posix()
        if (
            "\\" in relative
            or ":" in relative
            or any(ord(char) < 32 for char in relative)
        ):
            raise ValueError(
                f"Source path is unsafe for portable extraction: {relative!r}"
            )
    return sorted(files, key=lambda path: path.relative_to(root).as_posix())


def git_revision(root):
    if not (root / ".git").exists():
        return None
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--verify", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
            timeout=10,
        )
        return result.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None


def create_candidate(root, output):
    root = Path(root).resolve(strict=True)
    output = Path(output).resolve()
    if output == root or any(
        output.is_relative_to(root / name) for name in SOURCE_ROOTS
    ):
        raise ValueError("Output must be outside all included source trees")
    files = source_files(root)
    manifest = {
        "format_version": 1,
        "source_kind": "working-tree snapshot",
        "git_revision": git_revision(root),
        "provenance_note": "Git revision identifies HEAD only; files include current working-tree contents, including uncommitted files.",
        "files": [],
    }
    output.mkdir(parents=True, exist_ok=False)
    try:
        archive = output / "choscordb-source.tar.gz"
        with archive.open("xb") as raw:
            with gzip.GzipFile(
                filename="", fileobj=raw, mode="wb", mtime=0
            ) as compressed:
                with tarfile.open(
                    fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
                ) as bundle:
                    for path in files:
                        data, mode = read_source(path)
                        relative = path.relative_to(root).as_posix()
                        info = tarfile.TarInfo(f"choscordb-source/{relative}")
                        info.size = len(data)
                        info.mode = 0o755 if mode & 0o111 else 0o644
                        info.uid = info.gid = info.mtime = 0
                        info.uname = info.gname = ""
                        bundle.addfile(info, io.BytesIO(data))
                        manifest["files"].append(
                            {
                                "path": relative,
                                "size": len(data),
                                "sha256": hashlib.sha256(data).hexdigest(),
                            }
                        )
        manifest_file = output / "manifest.json"
        with manifest_file.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(
                json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
            )
        with (output / "SHA256SUMS").open(
            "x", encoding="ascii", newline="\n"
        ) as stream:
            for artifact in (archive, manifest_file):
                with artifact.open("rb") as source:
                    digest = hashlib.file_digest(source, "sha256").hexdigest()
                stream.write(f"{digest}  {artifact.name}\n")
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", type=Path, default=Path(__file__).resolve().parents[2]
    )
    parser.add_argument(
        "--output", type=Path, required=True, help="New output directory"
    )
    args = parser.parse_args()
    output = create_candidate(args.root, args.output)
    print(f"Source candidate prepared in {output}; nothing published.")


if __name__ == "__main__":
    main()
