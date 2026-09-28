#!/usr/bin/env python3
"""Attach verified Windows and Linux assets to an existing macOS GitHub Release."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

from publish import GitHubStore


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def attach(repo, tag, windows, linux, dry_run=False, store=None):
    if not re.fullmatch(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", tag):
        raise ValueError("Expected a stable vX.Y.Z tag")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected an OWNER/REPO GitHub repository")
    version = tag[1:]
    if windows.name != f"ChoscorDB-{version}-windows-x64.zip":
        raise ValueError("Windows asset version or name mismatch")
    if linux.name != f"ChoscorDB-{version}-linux-x86_64.AppImage":
        raise ValueError("Linux asset version or name mismatch")
    if not windows.is_file() or not linux.is_file():
        raise ValueError("Both platform assets are required")
    expected = digest(windows)
    local_commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], text=True
    ).strip()
    local_tag_commit = subprocess.check_output(
        ["git", "rev-parse", f"refs/tags/{tag}^{{commit}}"], text=True
    ).strip()
    if local_commit != local_tag_commit:
        raise ValueError("Checkout is not the selected release tag")
    store = store or GitHubStore(repo)
    if store.tag_commit(tag) != local_commit:
        raise ValueError("Remote tag differs from checked-out source commit")
    releases = [item for item in store.releases() if item["tag_name"] == tag]
    if len(releases) != 1 or releases[0]["draft"] or releases[0]["prerelease"]:
        raise ValueError("Expected exactly one published stable GitHub Release")
    remote = store.assets(releases[0])
    checksums = windows.parent / f"ChoscorDB-{version}-windows-linux-SHA256SUMS"
    checksums.write_text(f"{expected}  {windows.name}\n{digest(linux)}  {linux.name}\n")
    for path in (windows, linux, checksums):
        local_digest = digest(path)
        if path.name in remote:
            if store.digest(remote[path.name]) != local_digest:
                raise ValueError(f"Conflicting published asset: {path.name}")
            continue
        if dry_run:
            continue
        store.upload(tag, path)
        remote = store.assets(releases[0])
        if path.name not in remote or store.digest(remote[path.name]) != local_digest:
            raise ValueError(f"Uploaded asset differs: {path.name}")
    if not dry_run:
        for path in (windows, linux, checksums):
            store.available(
                f"https://github.com/{repo}/releases/download/{tag}/{path.name}",
                digest(path),
            )
    return [windows.name, linux.name, checksums.name]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo", default=os.environ.get("GITHUB_REPOSITORY"), required=False
    )
    parser.add_argument("--tag", required=True)
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--linux", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        if not args.repo:
            raise ValueError("--repo or GITHUB_REPOSITORY is required")
        print(
            attach(
                args.repo,
                args.tag,
                args.windows.resolve(),
                args.linux.resolve(),
                args.dry_run,
            )
        )
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release attachment failed: {error}\n")


if __name__ == "__main__":
    main()
