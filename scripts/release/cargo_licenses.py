#!/usr/bin/env python3
"""Collect license texts for the normal Rust dependency closure into a local sidecar."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from notices_sbom import normal_dependency_packages, relative

LICENSE_PREFIXES = ("license", "licence", "copying", "notice", "unlicense")


def license_files(root):
    if not root.is_dir() or root.is_symlink():
        return []
    result = []
    for path in sorted(root.iterdir(), key=lambda item: item.name.lower()):
        if (path.is_file() and not path.is_symlink() and
                path.name.lower().startswith(LICENSE_PREFIXES)):
            data = path.read_bytes()
            if not data.strip():
                raise ValueError(f"Empty Cargo license text: {path.name}")
            data.decode("utf-8")
            result.append((path.name, data))
    return result


def collect(metadata_path, fallback, output, root_package):
    metadata_path = Path(metadata_path).resolve(strict=True)
    fallback = Path(fallback).resolve(strict=True)
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    metadata = json.loads(metadata_path.read_bytes())
    packages = normal_dependency_packages(metadata, root_package)
    rows = []
    prepared = []
    for package in packages:
        coordinate = package["name"] + "@" + package["version"]
        relative(coordinate)
        manifest = Path(package["manifest_path"])
        if not manifest.is_absolute() or manifest.is_symlink() or not manifest.is_file():
            raise ValueError(f"Cargo manifest is missing or unsafe: {coordinate}")
        texts = license_files(manifest.parent)
        origin = "cargo-package"
        verification = None
        if not texts:
            override = fallback / coordinate
            texts = license_files(override)
            source_record = override / "source.json"
            if not texts or source_record.is_symlink() or not source_record.is_file():
                raise ValueError(f"License texts unavailable for {coordinate}")
            verification = json.loads(source_record.read_text())
            expected = verification.get("files")
            actual = {name: hashlib.sha256(data).hexdigest() for name, data in texts}
            if (verification.get("coordinate") != coordinate or expected != actual or
                    not str(verification.get("url", "")).startswith("https://")):
                raise ValueError(f"Verified fallback digest or source mismatch for {coordinate}")
            origin = "verified-fallback"
        prepared.append((coordinate, texts))
        rows.append({
            "name": package["name"], "version": package["version"],
            "license": package.get("license"), "source": package.get("source"),
            "origin": origin,
            "verification": verification,
            "files": [{"path": name, "sha256": hashlib.sha256(data).hexdigest()}
                      for name, data in texts],
        })
    output.mkdir(parents=True, exist_ok=False)
    try:
        for coordinate, texts in prepared:
            directory = output / coordinate
            directory.mkdir()
            for name, data in texts:
                (directory / name).write_bytes(data)
        (output / "index.json").write_text(
            json.dumps({"format_version": 1, "root_package": root_package,
                        "packages": rows}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cargo-metadata", type=Path, required=True)
    parser.add_argument("--fallback", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cargo-root-package", required=True)
    args = parser.parse_args()
    print(collect(args.cargo_metadata, args.fallback, args.output,
                  args.cargo_root_package))


if __name__ == "__main__":
    main()
