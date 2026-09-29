#!/usr/bin/env python3
"""Verify Windows/Linux candidates before the three-asset draft publisher runs.

This compatibility CLI deliberately cannot attach files to a public Release.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess

from publish import GitHubStore, candidate


def attach(repo, tag, windows, linux, dry_run=False, store=None):
    if not re.fullmatch(r"v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", tag):
        raise ValueError("Expected a stable vX.Y.Z tag")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected an OWNER/REPO GitHub repository")
    version = tag[1:]
    names = [
        f"ChoscorDB-{version}-windows-x64-setup.exe",
        f"ChoscorDB-{version}-linux-x86_64.AppImage",
    ]
    local_commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], text=True
    ).strip()
    digests = [
        candidate(path, name, local_commit)
        for path, name in zip((windows, linux), names)
    ]
    local_tag = subprocess.check_output(
        ["git", "rev-parse", f"refs/tags/{tag}^{{commit}}"], text=True
    ).strip()
    if local_commit != local_tag:
        raise ValueError("Checkout is not the selected release tag")
    store = store or GitHubStore(repo)
    if store.tag_commit(tag) != local_commit:
        raise ValueError("Remote tag differs from checked-out source commit")
    releases = [item for item in store.releases() if item["tag_name"] == tag]
    if len(releases) > 1 or any(not item["draft"] for item in releases):
        raise ValueError(
            "Existing public or ambiguous Release; use three-asset publisher"
        )
    if releases and releases[0]["prerelease"]:
        raise ValueError("Stable release conflicts with prerelease")
    return dict(zip(names, digests))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--windows", type=Path, required=True)
    parser.add_argument("--linux", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    try:
        if not args.repo:
            raise ValueError("--repo or GITHUB_REPOSITORY is required")
        print(attach(args.repo, args.tag, args.windows, args.linux))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Candidate verification failed: {error}\n")


if __name__ == "__main__":
    main()
