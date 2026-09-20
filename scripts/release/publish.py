#!/usr/bin/env python3
"""Explicit, serialized R2 publication. AWS CLI uses the maintainer's local credentials."""

import argparse
import contextlib
import fcntl
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

SPARKLE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", SPARKLE)
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
            or enclosure.get("url") != f"{base_url}/ChoscorDB-{version}.dmg"
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


def publish(
    root, manifest, store, dry_run=False, manifest_bytes=None, historical_verifier=None
):
    version = manifest["version"]
    current = version_tuple(version)
    base = manifest["base_url"].rstrip("/")
    if manifest["feed_url"] != base + "/" + FEED:
        raise ValueError("incompatible feed configuration")
    payload = {}
    for artifact in manifest["artifacts"]:
        name = artifact["path"]
        if Path(name).name != name or name in payload:
            raise ValueError("invalid artifact name")
        if name not in (LATEST, FEED) and not (
            name == f"ChoscorDB-{version}.dmg"
            or name.startswith(f"ChoscorDB-{version}-")
            or name.startswith(f"choscordb-{version}-")
        ):
            raise ValueError("metadata artifacts must be version-associated")
        data = (root / name).read_bytes()
        if (
            len(data) != artifact["size"]
            or hashlib.sha256(data).hexdigest() != artifact["sha256"]
        ):
            raise ValueError("artifact hash mismatch")
        payload[name] = data
    if manifest_bytes is not None:
        payload[f"ChoscorDB-{version}-manifest.json"] = manifest_bytes
    dmg = f"ChoscorDB-{version}.dmg"
    if not {dmg, LATEST, FEED}.issubset(payload) or payload[LATEST] != payload[dmg]:
        raise ValueError("incomplete release or mismatched latest")
    new_root, new_channel, new_versions = parse_feed(payload[FEED], base)
    if set(new_versions) != {version}:
        raise ValueError("package appcast must contain exactly selected release")
    if int(new_versions[version].find("enclosure").get("length")) != len(payload[dmg]):
        raise ValueError("appcast length mismatch")
    previous = store.get(FEED)
    if previous is not None:
        old_root, old_channel, old_versions = parse_feed(previous, base)
        if max(map(version_tuple, old_versions)) > current:
            raise ValueError("stale publication would replace newer release")
        for old_version, old_item in old_versions.items():
            key = f"ChoscorDB-{old_version}.dmg"
            old_payload = store.get(key)
            enclosure = old_item.find("enclosure")
            if old_payload is None or len(old_payload) != int(enclosure.get("length")):
                raise ValueError("missing or invalid historical payload: " + key)
            if historical_verifier is not None:
                historical_verifier(
                    old_payload, enclosure.get(f"{{{SPARKLE}}}edSignature")
                )
            store.available(key, old_payload)
        if version in old_versions:
            old_enclosure = old_versions[version].find("enclosure")
            if old_enclosure.attrib != new_versions[version].find("enclosure").attrib:
                raise ValueError("immutable appcast version conflict")
        else:
            old_channel.insert(0, new_versions[version])
        new_root = old_root
    merged = ET.tostring(new_root, encoding="utf-8", xml_declaration=True)
    immutable = {
        name: data for name, data in payload.items() if name not in (LATEST, FEED)
    }
    existing = {}
    for name, data in immutable.items():
        existing[name] = store.get(name)
        if existing[name] is not None and existing[name] != data:
            raise ValueError("immutable artifact conflict: " + name)
    # A latest alias without a feed indicates an interrupted first publication.
    # Only identical bytes are safe; otherwise its release age is unknowable.
    latest = store.get(LATEST)
    if previous is None and latest is not None and latest != payload[LATEST]:
        raise ValueError("unverifiable latest alias without feed")
    if previous is not None and latest != payload[LATEST]:
        newest = max(old_versions, key=version_tuple)
        published_dmg = store.get(f"ChoscorDB-{newest}.dmg")
        if published_dmg is None or latest != published_dmg:
            raise ValueError(
                "unverifiable latest alias; retry the interrupted release first"
            )
    for name in [*immutable, LATEST, FEED]:
        print(
            ("Would publish " if dry_run else "Publish ")
            + getattr(store, "bucket", "controlled-store")
            + "/"
            + name
            + " -> "
            + base
            + "/"
            + name
        )
    if dry_run:
        return
    for name, data in immutable.items():
        if existing[name] is None:
            store.put(name, data)
        store.available(name, data)
    # Detect unexpected remote changes before touching the mutable objects.
    if store.get(FEED) != previous or store.get(LATEST) != latest:
        raise ValueError("remote release changed during publication")
    store.put(LATEST, payload[LATEST])
    store.available(LATEST, payload[LATEST])
    store.put(FEED, merged)
    store.available(FEED, merged)


def verify_historical_signature(sparkle_tools, data, signature):
    from macos import run

    with tempfile.TemporaryDirectory(prefix="choscordb-history-verify-") as directory:
        artifact = Path(directory) / "update.dmg"
        artifact.write_bytes(data)
        run(
            [
                Path(sparkle_tools) / "bin/sign_update",
                "--account",
                "com.choscor.ChoscorDB",
                "--verify",
                artifact,
                signature,
            ]
        )


class R2Store:
    def __init__(self, bucket, endpoint, base_url):
        self.bucket, self.endpoint, self.base_url = (
            bucket,
            endpoint,
            base_url.rstrip("/"),
        )

    def command(self, arguments):
        result = subprocess.run(
            ["aws", "--endpoint-url", self.endpoint, "s3api", *arguments],
            capture_output=True,
            timeout=120,
        )
        if result.returncode:
            # Do not print CLI stderr: provider diagnostics may contain account details.
            if b"(NoSuchKey)" in result.stderr:
                return False
            raise ValueError(
                "R2 request failed; check local AWS credentials/network configuration"
            )
        return True

    def get(self, key):
        with tempfile.TemporaryDirectory(prefix="choscordb-read-") as directory:
            path = Path(directory) / "object"
            if not self.command(
                ["get-object", "--bucket", self.bucket, "--key", key, str(path)]
            ):
                return None
            return path.read_bytes()

    def put(self, key, data):
        with tempfile.TemporaryDirectory(prefix="choscordb-write-") as directory:
            path = Path(directory) / "object"
            path.write_bytes(data)
            kind = (
                "application/xml"
                if key.endswith(".xml")
                else "application/octet-stream"
            )
            if not self.command(
                [
                    "put-object",
                    "--bucket",
                    self.bucket,
                    "--key",
                    key,
                    "--body",
                    str(path),
                    "--content-type",
                    kind,
                    "--cache-control",
                    "no-cache"
                    if key in (LATEST, FEED)
                    else "public, max-age=31536000, immutable",
                ]
            ):
                raise ValueError("R2 upload failed")

    def available(self, key, data):
        request = urllib.request.Request(
            self.base_url + "/" + key, headers={"Cache-Control": "no-cache"}
        )
        with urllib.request.urlopen(request, timeout=60) as response:
            digest = hashlib.file_digest(response, "sha256").hexdigest()
        if digest != hashlib.sha256(data).hexdigest():
            raise ValueError("public download unavailable or bytes differ: " + key)


@contextlib.contextmanager
def publication_lock():
    # Per-user, independent of checkout, bucket, and selected manifest.
    path = Path(tempfile.gettempdir()) / f"choscordb-publish-{os.getuid()}.lock"
    with path.open("a") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("another local publication is running") from error
        yield


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument(
        "--sparkle-tools",
        type=Path,
        help="Local Sparkle tool root; defaults to CHOSCORDB_SPARKLE_TOOLS or build/release-dependencies/sparkle",
    )
    parser.add_argument(
        "--version", help="Optional assertion against selected manifest"
    )
    parser.add_argument(
        "--bucket", default=os.environ.get("R2_BUCKET", "choscor-downloads")
    )
    parser.add_argument("--endpoint-url", default=os.environ.get("R2_ENDPOINT_URL"))
    parser.add_argument(
        "--base-url",
        default=os.environ.get("CHOSCORDB_RELEASE_BASE_URL", "https://cdn.choscor.com"),
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        from macos import resolve_sparkle_tools, verify_manifest

        sparkle_tools = resolve_sparkle_tools(args.sparkle_tools)
        manifest = verify_manifest(args.manifest.resolve(), sparkle_tools)
        if args.version and args.version != manifest["version"]:
            raise ValueError("selected version does not match verified manifest")
        if args.base_url.rstrip("/") != manifest["base_url"]:
            raise ValueError("incompatible feed configuration")
        if not args.endpoint_url or not args.endpoint_url.startswith("https://"):
            raise ValueError("set R2_ENDPOINT_URL to the HTTPS R2 S3 endpoint")
        store = R2Store(args.bucket, args.endpoint_url, args.base_url)
        # Dry-run does not create even a local lock file.
        with contextlib.nullcontext() if args.dry_run else publication_lock():
            print(f"Destination: R2 bucket {args.bucket}; public URL {args.base_url}")
            publish(
                args.manifest.resolve().parent,
                manifest,
                store,
                args.dry_run,
                args.manifest.read_bytes(),
                lambda data, signature: verify_historical_signature(
                    sparkle_tools, data, signature
                ),
            )
        return 0
    except (
        ValueError,
        OSError,
        KeyError,
        ET.ParseError,
        subprocess.TimeoutExpired,
    ) as error:
        print("Publication refused: " + str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
