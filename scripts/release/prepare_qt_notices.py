#!/usr/bin/env python3
"""Prepare pinned Qt source license metadata for release staging and SBOM generation."""

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import posixpath
import tarfile
import urllib.request

VERSION = "6.8.3"
URL = (
    "https://download.qt.io/archive/qt/6.8/6.8.3/submodules/"
    "qtbase-everywhere-src-6.8.3.tar.xz"
)
SHA256 = "56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80"
ROOT = f"qtbase-everywhere-src-{VERSION}"
SVG_SHA256 = "35eb516460f00f264eb504baa253432384351cf23fb9980a5857190e8deef438"
SVG_URL = URL.replace("qtbase-", "qtsvg-")


def verify_archive(path, expected=None):
    with path.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if digest != (expected or SHA256):
        raise ValueError("Qt source SHA-256 mismatch; refusing to inspect it")


def selected_path(relative):
    if relative == "REUSE.toml":
        return Path("REUSE.toml")
    if relative == ".reuse/dep5":
        return Path("reuse/dep5")
    if relative.startswith("LICENSES/"):
        return Path(relative)
    path = PurePosixPath(relative)
    if path.name == "qt_attribution.json":
        return Path("attributions", *path.parts)
    return None


def prepare(archive, output, svg_archive=None):
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    inputs = [("qtbase", Path(archive), SHA256, URL)]
    if svg_archive is not None:
        inputs.append(("qtsvg", Path(svg_archive), SVG_SHA256, SVG_URL))
    for _, path, checksum, _ in inputs:
        verify_archive(path, checksum)
    output.mkdir(parents=True, exist_ok=False)
    modules = {}
    try:
        for module, archive, checksum, url in inputs:
            root = f"{module}-everywhere-src-{VERSION}"
            copied = {}
            with tarfile.open(archive, "r:xz") as source:
                members = {}
                for member in source.getmembers():
                    path = PurePosixPath(member.name)
                    if (
                        path.is_absolute()
                        or ".." in path.parts
                        or not path.parts
                        or path.parts[0] != root
                        or "\\" in member.name
                    ):
                        raise ValueError(f"Unsafe Qt source member: {member.name!r}")
                    relative = PurePosixPath(*path.parts[1:]).as_posix()
                    if relative in members:
                        raise ValueError("Duplicate Qt source member")
                    members[relative] = member
                selected = {
                    name: selected_path(name)
                    for name in members
                    if selected_path(name) is not None
                }
                for name in list(selected):
                    if not name.endswith("qt_attribution.json"):
                        continue
                    member = members[name]
                    if not member.isfile():
                        raise ValueError("Qt attribution is not regular")
                    # Pinned Qt attribution records use literal multiline text
                    # strings. Preserve their bytes while accepting that upstream
                    # format; archive digests and referenced paths stay validated.
                    records = json.loads(
                        source.extractfile(member).read(), strict=False
                    )
                    if isinstance(records, dict):
                        records = [records]
                    for record in records:
                        refs = record.get("LicenseFile", [])
                        if isinstance(refs, str):
                            refs = [refs]
                        for ref in refs:
                            if PurePosixPath(ref).is_absolute() or "\\" in ref:
                                raise ValueError("Unsafe attribution license reference")
                            resolved = posixpath.normpath(
                                posixpath.join(posixpath.dirname(name), ref)
                            )
                            if (
                                resolved == ".."
                                or resolved.startswith("../")
                                or resolved not in members
                            ):
                                raise ValueError(
                                    "Missing or escaping attribution license reference"
                                )
                            selected[resolved] = Path("referenced", resolved)
                for name, destination in sorted(selected.items()):
                    member = members[name]
                    if member.isdir():
                        continue
                    if not member.isfile():
                        raise ValueError("Selected Qt notice is not a regular file")
                    if module != "qtbase":
                        destination = Path(module) / destination
                    data = source.extractfile(member).read()
                    target = output / destination
                    if target.exists():
                        raise ValueError("Duplicate copied Qt notice")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(data)
                    copied[destination.as_posix()] = hashlib.sha256(data).hexdigest()
                if not any(
                    name.startswith(("LICENSES/", module + "/LICENSES/"))
                    for name in copied
                ):
                    raise ValueError("Qt source contains no usable LICENSES files")
            modules[module] = {
                "version": VERSION,
                "url": url,
                "sha256": checksum,
                "copied_files": [
                    {"path": name, "sha256": digest}
                    for name, digest in sorted(copied.items())
                ],
            }
        record = {
            "version": VERSION,
            "license": "LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
            "url": URL,
            "modules": modules,
        }
        (output / "source.json").write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--qtsvg-archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    archive = args.archive
    if archive is None:
        archive = args.output.parent / f"qtbase-everywhere-src-{VERSION}.tar.xz"
        if not archive.exists():
            temporary = archive.with_suffix(".download")
            with (
                urllib.request.urlopen(URL, timeout=120) as response,
                temporary.open("wb") as target,
            ):
                shutil.copyfileobj(response, target)
            verify_archive(temporary)
            temporary.replace(archive)
    svg = args.qtsvg_archive
    if svg is None:
        svg = args.output.parent / f"qtsvg-everywhere-src-{VERSION}.tar.xz"
        if not svg.exists():
            temporary = svg.with_suffix(".download")
            with (
                urllib.request.urlopen(SVG_URL, timeout=120) as response,
                temporary.open("wb") as target,
            ):
                shutil.copyfileobj(response, target)
            verify_archive(temporary, SVG_SHA256)
            temporary.replace(svg)
    print(prepare(archive, args.output, svg))


if __name__ == "__main__":
    main()
