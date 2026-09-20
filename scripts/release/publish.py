#!/usr/bin/env python3
"""Publish verified macOS artifacts exclusively through GitHub Releases."""

import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
FEED = "choscordb-appcast.xml"
LATEST = "ChoscorDB.dmg"


def version_tuple(value):
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value):
        raise ValueError("invalid stable release version")
    return tuple(map(int, value.split(".")))


def parse_feed(data, base_url):
    root = ET.fromstring(data)
    channel = root.find("channel")
    if root.tag != "rss" or channel is None:
        raise ValueError("invalid appcast channel")
    versions = {}
    for item in channel.findall("item"):
        enclosure = item.find("enclosure")
        if enclosure is None:
            raise ValueError("missing appcast enclosure")
        version = enclosure.get(f"{{{SPARKLE}}}version", "")
        version_tuple(version)
        for field in ("version", "shortVersionString"):
            children = item.findall(f"{{{SPARKLE}}}{field}")
            if len(children) > 1 or any(child.text != version for child in children):
                raise ValueError("conflicting appcast version metadata")
        if (
            version in versions
            or enclosure.get("url")
            != f"{base_url}/download/v{version}/ChoscorDB-{version}.dmg"
        ):
            raise ValueError("incompatible feed configuration")
        if enclosure.get(
            f"{{{SPARKLE}}}shortVersionString"
        ) != version or not enclosure.get(f"{{{SPARKLE}}}edSignature"):
            raise ValueError("invalid signed appcast entry")
        if enclosure.get("type") != "application/octet-stream":
            raise ValueError("invalid appcast enclosure type")
        minimum = item.find(f"{{{SPARKLE}}}minimumSystemVersion")
        if minimum is None or minimum.text != "26.0":
            raise ValueError("incompatible appcast minimum OS")
        if not enclosure.get("length", "").isdigit():
            raise ValueError("invalid appcast length")
        versions[version] = item
    if not versions:
        raise ValueError("empty appcast")
    return root, channel, versions


@contextlib.contextmanager
def publication_lock():
    # Per-user, independent of checkout, repository, and selected manifest.
    path = Path(tempfile.gettempdir()) / f"choscordb-publish-{os.getuid()}.lock"
    with path.open("a") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("another local publication is running") from error
        yield


def publish(root, manifest, store, dry_run=False, notes_file=None):
    from macos import release_base, feed_url, artifact_url

    version = manifest["version"]
    current = version_tuple(version)
    base = release_base(manifest["base_url"])
    if manifest["feed_url"] != feed_url(base):
        raise ValueError("incompatible feed configuration")
    payload = {}
    for artifact in manifest["artifacts"]:
        name = artifact["path"]
        if Path(name).name != name or name in payload:
            raise ValueError("invalid artifact name")
        path = root / name
        with path.open("rb") as handle:
            digest = hashlib.file_digest(handle, "sha256").hexdigest()
        if path.stat().st_size != artifact["size"] or digest != artifact["sha256"]:
            raise ValueError("artifact hash mismatch")
        payload[name] = digest
    names = [f"ChoscorDB-{version}.dmg", LATEST, FEED]
    if not set(names).issubset(payload) or payload[names[0]] != payload[LATEST]:
        raise ValueError("incomplete release or mismatched latest")
    _, _, versions = parse_feed((root / FEED).read_bytes(), base)
    if set(versions) != {version}:
        raise ValueError("package appcast must contain exactly selected release")
    if (
        int(versions[version].find("enclosure").get("length"))
        != (root / names[0]).stat().st_size
    ):
        raise ValueError("appcast length mismatch")
    tag = "v" + version
    notes = notes_file.read_text(encoding="utf-8").rstrip("\n") if notes_file else None

    def preflight():
        if store.tag_commit(tag) != manifest["source_commit"]:
            raise ValueError("remote tag does not match verified source commit")
        releases = store.releases()
        for release in releases:
            if release["draft"] or release["prerelease"]:
                continue
            candidate = release["tag_name"]
            if re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", candidate):
                if version_tuple(candidate[1:]) > current:
                    raise ValueError("stale publication would replace newer release")
        matching = [r for r in releases if r["tag_name"] == tag]
        if len(matching) > 1:
            raise ValueError("ambiguous remote release")
        selected = matching[0] if matching else None
        if selected is not None and notes is not None:
            body = selected.get("body") or ""
            if (
                not isinstance(body, str)
                or body.replace("\r\n", "\n").rstrip("\n") != notes
            ):
                raise ValueError("release notes conflict; review remote notes manually")
        return selected

    release = preflight()
    if release and release["prerelease"]:
        raise ValueError("stable release conflicts with prerelease")
    assets = store.assets(release) if release else {}
    if set(assets) - set(names):
        raise ValueError("unexpected assets in remote release; review manually")
    for name, asset in assets.items():
        if store.digest(asset) != payload[name]:
            raise ValueError("immutable artifact conflict: " + name)
    if release and not release["draft"] and set(assets) != set(names):
        raise ValueError("public release is incomplete; review manually")
    for name in names:
        print(
            ("Would publish " if dry_run else "Verify/publish ")
            + artifact_url(base, version, name)
        )
    if dry_run:
        return
    if notes_file is None:
        raise ValueError("release notes file is required")
    if release is None:
        preflight()
        release = store.create(tag, notes_file)
    if release["draft"]:
        for name in names:
            fresh = preflight()
            if fresh is None or fresh["id"] != release["id"] or not fresh["draft"]:
                raise ValueError("remote release changed during publication")
            assets = store.assets(fresh)
            if set(assets) - set(names):
                raise ValueError("unexpected remote assets")
            if name not in assets:
                store.upload(tag, root / name)
                assets = store.assets(fresh)
            if name not in assets or store.digest(assets[name]) != payload[name]:
                raise ValueError("uploaded asset bytes differ: " + name)
        fresh = preflight()
        if fresh is None or fresh["id"] != release["id"] or not fresh["draft"]:
            raise ValueError("remote release changed before publication")
        assets = store.assets(fresh)
        if set(assets) != set(names) or any(
            store.digest(assets[name]) != payload[name] for name in names
        ):
            raise ValueError("remote assets changed before publication")
        try:
            store.finish(tag)
        except ValueError as error:
            raise ValueError(
                "Release may already be published; retry to verify remote state before announcing"
            ) from error
    try:
        for name in names:
            store.available(artifact_url(base, version, name), payload[name])
        store.available(base + "/latest/download/" + LATEST, payload[LATEST])
        store.available(feed_url(base), payload[FEED])
    except ValueError as error:
        raise ValueError(
            "Release is published, but public verification failed; retry verification before announcing"
        ) from error


class GitHubStore:
    def __init__(self, repo):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
            raise ValueError("invalid GitHub repository")
        self.repo = repo

    def command(self, arguments, output=None):
        try:
            result = subprocess.run(
                ["gh", *arguments],
                stdout=output if output else subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=600,
                env={**os.environ, "GH_HOST": "github.com", "GH_PROMPT_DISABLED": "1"},
            )
        except (OSError, subprocess.TimeoutExpired):
            raise ValueError(
                "GitHub request failed; check local gh authentication/network"
            ) from None
        if result.returncode:
            raise ValueError(
                "GitHub request failed; check local gh authentication/network"
            )
        return result.stdout

    def api(self, endpoint):
        try:
            return json.loads(
                self.command(["api", "repos/" + self.repo + "/" + endpoint])
            )
        except (json.JSONDecodeError, UnicodeError):
            raise ValueError("invalid GitHub response") from None

    def tag_commit(self, tag):
        obj = self.api("git/ref/tags/" + tag)["object"]
        for _ in range(8):
            if obj["type"] == "commit":
                return obj["sha"]
            if obj["type"] != "tag":
                break
            obj = self.api("git/tags/" + obj["sha"])["object"]
        raise ValueError("remote tag does not resolve to a commit")

    def releases(self):
        # A successful complete list establishes absence; no HTTP failure means absent.
        records = []
        page = 1
        while True:
            batch = self.api(f"releases?per_page=100&page={page}")
            if not isinstance(batch, list):
                raise ValueError("invalid GitHub release list")
            records.extend(batch)
            if len(batch) < 100:
                return records
            page += 1

    def create(self, tag, notes):
        self.command(
            [
                "release",
                "create",
                tag,
                "--repo",
                self.repo,
                "--verify-tag",
                "--draft",
                "--title",
                "ChoscorDB " + tag[1:],
                "--notes-file",
                str(notes),
            ]
        )
        return self.api("releases/tags/" + tag)

    def assets(self, release):
        result = {}
        page = 1
        while True:
            batch = self.api(
                f"releases/{release['id']}/assets?per_page=100&page={page}"
            )
            if not isinstance(batch, list):
                raise ValueError("invalid GitHub asset list")
            for asset in batch:
                if asset["name"] in result:
                    raise ValueError("duplicate remote asset")
                result[asset["name"]] = asset
            if len(batch) < 100:
                return result
            page += 1

    def digest(self, asset):
        with tempfile.TemporaryFile() as handle:
            self.command(
                [
                    "api",
                    "repos/" + self.repo + "/releases/assets/" + str(asset["id"]),
                    "-H",
                    "Accept: application/octet-stream",
                ],
                output=handle,
            )
            handle.seek(0)
            return hashlib.file_digest(handle, "sha256").hexdigest()

    def upload(self, tag, path):
        self.command(["release", "upload", tag, str(path), "--repo", self.repo])

    def finish(self, tag):
        self.command(
            ["release", "edit", tag, "--repo", self.repo, "--draft=false", "--latest"]
        )

    def available(self, url, digest):
        with tempfile.TemporaryDirectory(prefix="choscordb-download-") as directory:
            path = Path(directory) / "object"
            try:
                result = subprocess.run(
                    [
                        "curl",
                        "--disable",
                        "--fail",
                        "--silent",
                        "--show-error",
                        "--location",
                        "--proto",
                        "=https",
                        "--proto-redir",
                        "=https",
                        "--connect-timeout",
                        "30",
                        "--max-time",
                        "600",
                        "--header",
                        "Cache-Control: no-cache",
                        "--output",
                        str(path),
                        url,
                    ],
                    capture_output=True,
                    timeout=610,
                )
            except (OSError, subprocess.TimeoutExpired):
                raise ValueError("public download failed; check curl/network") from None
            if result.returncode:
                raise ValueError("public download failed; check public URL/network")
            with path.open("rb") as handle:
                actual = hashlib.file_digest(handle, "sha256").hexdigest()
            if actual != digest:
                raise ValueError("public download bytes differ")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--sparkle-tools", type=Path)
    parser.add_argument("--notes-file", type=Path)
    parser.add_argument(
        "--repo", help="Optional assertion against manifest GitHub repository"
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        from macos import release_base, resolve_sparkle_tools, verify_manifest

        if not args.dry_run and (
            args.notes_file is None or not args.notes_file.is_file()
        ):
            raise ValueError("provide --notes-file with reviewed public release notes")
        manifest = verify_manifest(
            args.manifest.resolve(), resolve_sparkle_tools(args.sparkle_tools)
        )
        base = release_base(manifest["base_url"])
        repo = base.removeprefix("https://github.com/").removesuffix("/releases")
        if args.repo and args.repo != repo:
            raise ValueError("repository does not match verified manifest")
        with contextlib.nullcontext() if args.dry_run else publication_lock():
            publish(
                args.manifest.resolve().parent,
                manifest,
                GitHubStore(repo),
                args.dry_run,
                args.notes_file,
            )
        return 0
    except (ValueError, KeyError, ET.ParseError) as error:
        print("Publication refused: " + str(error))
        return 1
    except (OSError, subprocess.TimeoutExpired):
        print("Publication refused: local tool or file operation failed")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
