#!/usr/bin/env python3
"""Sign stable Windows/Linux feeds and advance GitHub Pages after publication."""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

from macos import (
    ACCOUNT,
    artifact_url,
    feed_url,
    release_base,
    resolve_sparkle_tools,
    stable,
    verify_live_feed,
    verify_manifest,
)
from publish import GitHubStore, candidate, parse_feed

PUBLIC_KEY = "tWwf5+4hAmilViw6CrynbGYUr5ISGt/4OOAKR5jm+0o="
KEY_ID = "windows-linux-v1"
DER_PREFIX = bytes.fromhex("302a300506032b6570032100")
FEEDS = {
    "windows": ("x64", "windows-x64.json", "windows-x64-setup.exe"),
    "linux": ("x86_64", "linux-x86_64.json", "linux-x86_64.AppImage"),
}


def _openssl(*args):
    result = subprocess.run(["openssl", *map(str, args)], capture_output=True)
    if result.returncode:
        raise ValueError("Ed25519 signing or verification failed")
    return result.stdout


def public_from_private(path):
    der = _openssl("pkey", "-in", path, "-pubout", "-outform", "DER")
    if len(der) != 44 or not der.startswith(DER_PREFIX):
        raise ValueError("expected an Ed25519 private key")
    return base64.b64encode(der[len(DER_PREFIX) :]).decode()


def create_private_key(path):
    """Create a dedicated key without ever replacing an existing private file."""
    target = Path(path).expanduser()
    if target.exists() or target.is_symlink():
        raise ValueError("private update key already exists; refusing replacement")
    target.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=".update-signing-",
            suffix=".pem",
            dir=target.parent,
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
        temporary.chmod(0o600)
        _openssl("genpkey", "-algorithm", "Ed25519", "-out", temporary)
        public = public_from_private(temporary)
        try:
            os.link(temporary, target)
        except FileExistsError as error:
            raise ValueError(
                "private update key already exists; refusing replacement"
            ) from error
        return public
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _canonical(value):
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _sign(payload, private):
    with tempfile.TemporaryDirectory(prefix="choscordb-sign-") as directory:
        message = Path(directory) / "payload"
        message.write_bytes(payload)
        return _openssl("pkeyutl", "-sign", "-rawin", "-inkey", private, "-in", message)


def _verify(payload, signature, public):
    raw = base64.b64decode(public, validate=True)
    if len(raw) != 32 or len(signature) != 64:
        raise ValueError("invalid update signature or key")
    with tempfile.TemporaryDirectory(prefix="choscordb-verify-") as directory:
        root = Path(directory)
        (root / "payload").write_bytes(payload)
        (root / "signature").write_bytes(signature)
        public_pem = (
            b"-----BEGIN PUBLIC KEY-----\n"
            + base64.encodebytes(DER_PREFIX + raw)
            + b"-----END PUBLIC KEY-----\n"
        )
        (root / "public.pem").write_bytes(public_pem)
        try:
            _openssl(
                "pkeyutl",
                "-verify",
                "-rawin",
                "-pubin",
                "-inkey",
                root / "public.pem",
                "-in",
                root / "payload",
                "-sigfile",
                root / "signature",
            )
        except ValueError as error:
            raise ValueError("update signature verification failed") from error


def signed_metadata(version, platform, arch, package, base, notes, private, public):
    stable(version)
    base = release_base(base)
    if platform not in FEEDS or (
        arch,
        package.name.removeprefix(f"ChoscorDB-{version}-"),
    ) != (FEEDS[platform][0], FEEDS[platform][2]):
        raise ValueError("platform package name or architecture mismatch")
    if not isinstance(notes, str) or len(notes) > 8192 or str(Path.home()) in notes:
        raise ValueError("release notes are invalid for public metadata")
    if public_from_private(private) != public:
        raise ValueError("private key differs from the dedicated update key")
    with package.open("rb") as handle:
        checksum = hashlib.file_digest(handle, "sha256").hexdigest()
    payload = _canonical(
        {
            "version": version,
            "platform": platform,
            "arch": arch,
            "url": artifact_url(base, version, package.name),
            "size": package.stat().st_size,
            "sha256": checksum,
            "notes": notes,
        }
    )
    envelope = _canonical(
        {
            "key_id": KEY_ID,
            "payload": base64.b64encode(payload).decode(),
            "signature": base64.b64encode(_sign(payload, private)).decode(),
        }
    )
    verify_metadata(envelope, public)
    return envelope + b"\n"


def verify_metadata(data, public):
    try:
        envelope = json.loads(data)
        if (
            set(envelope) != {"key_id", "payload", "signature"}
            or envelope["key_id"] != KEY_ID
        ):
            raise ValueError("unsupported update key identity")
        payload = base64.b64decode(envelope["payload"], validate=True)
        signature = base64.b64decode(envelope["signature"], validate=True)
        _verify(payload, signature, public)
        record = json.loads(payload)
        if _canonical(record) != payload or set(record) != {
            "version",
            "platform",
            "arch",
            "url",
            "size",
            "sha256",
            "notes",
        }:
            raise ValueError("invalid canonical update metadata")
        stable(record["version"])
        if (
            record["platform"] not in FEEDS
            or record["arch"] != FEEDS[record["platform"]][0]
            or not isinstance(record["size"], int)
            or record["size"] <= 0
            or not re.fullmatch(r"[0-9a-f]{64}", record["sha256"])
        ):
            raise ValueError("invalid update metadata values")
        name = f"ChoscorDB-{record['version']}-{FEEDS[record['platform']][2]}"
        if not record["url"].endswith("/download/v" + record["version"] + "/" + name):
            raise ValueError("invalid immutable update URL")
        release_base(record["url"].split("/download/v", 1)[0])
        return record
    except (KeyError, TypeError, json.JSONDecodeError, UnicodeError) as error:
        raise ValueError("invalid signed update metadata") from error


def _artifact(manifest, name):
    matches = [item for item in manifest["artifacts"] if item["path"] == name]
    if len(matches) != 1:
        raise ValueError("missing local release artifact: " + name)
    return matches[0]


def deploy(
    root,
    manifest,
    windows,
    linux,
    notes,
    private,
    store,
    pages,
    public=PUBLIC_KEY,
    dry_run=False,
    wait_seconds=0,
):
    """Verify the already-public three-asset Release, then advance all feeds."""
    version = manifest["version"]
    stable(version)
    base = release_base(manifest["base_url"])
    url = feed_url(base)
    if manifest["feed_url"] != url:
        raise ValueError("macOS Pages feed does not match the release repository")
    repo = base.removeprefix("https://github.com/").removesuffix("/releases")
    tag = "v" + version
    if store.tag_commit(tag) != manifest["source_commit"]:
        raise ValueError("remote tag differs from the verified source commit")
    matches = [item for item in store.releases() if item["tag_name"] == tag]
    if len(matches) != 1 or matches[0]["draft"] or matches[0]["prerelease"]:
        raise ValueError("expected one published stable Release")
    if (matches[0].get("body") or "").replace("\r\n", "\n").rstrip(
        "\n"
    ) != notes.rstrip("\n"):
        raise ValueError("release notes conflict with published Release")
    names = [
        f"ChoscorDB-{version}.dmg",
        f"ChoscorDB-{version}-windows-x64-setup.exe",
        f"ChoscorDB-{version}-linux-x86_64.AppImage",
    ]
    paths = {
        names[0]: Path(root) / names[0],
        names[1]: Path(windows),
        names[2]: Path(linux),
    }
    expected = {}
    for name in (names[0], "choscordb-appcast.xml"):
        path = Path(root) / name
        artifact = _artifact(manifest, name)
        with path.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        if checksum != artifact["sha256"] or path.stat().st_size != artifact["size"]:
            raise ValueError("local macOS release artifact changed")
        expected[name] = checksum
    for name in names[1:]:
        expected[name] = candidate(paths[name], name, manifest["source_commit"])
    assets = store.assets(matches[0])
    if set(assets) != set(names):
        raise ValueError("published Release must have exactly three package assets")
    for name in names:
        if store.digest(assets[name]) != expected[name]:
            raise ValueError("published package bytes differ from verified candidate")
        store.available(artifact_url(base, version, name), expected[name])
    appcast = (Path(root) / "choscordb-appcast.xml").read_bytes()
    _, _, versions = parse_feed(appcast, base)
    if set(versions) != {version}:
        raise ValueError("local appcast version mismatch")
    prior = pages.fetch(url)
    if prior is None:
        raise ValueError("Pages appcast is absent; bootstrap Pages before packaging")
    _, _, prior_versions = parse_feed(prior, base)
    if any(
        tuple(map(int, item.split("."))) > tuple(map(int, version.split(".")))
        for item in prior_versions
    ):
        raise ValueError("Pages already advertises a newer version")
    files = {"choscordb-appcast.xml": appcast}
    for platform, path in (("windows", Path(windows)), ("linux", Path(linux))):
        arch, filename, _ = FEEDS[platform]
        current = pages.fetch(url.rsplit("/", 1)[0] + "/" + filename)
        if current is not None:
            current_record = verify_metadata(current, public)
            if tuple(map(int, current_record["version"].split("."))) > tuple(
                map(int, version.split("."))
            ):
                raise ValueError("Pages already advertises a newer version")
        files[filename] = signed_metadata(
            version, platform, arch, path, base, notes.rstrip("\n"), private, public
        )
    if dry_run:
        return files
    pages.publish(repo, files)
    try:
        _wait_for_pages(pages, base, files, wait_seconds)
    except ValueError as error:
        raise ValueError(
            "Release remains published; Pages verification failed, retry deploy"
        ) from error
    return files


class GitHubPages:
    """Publish three files as one fast-forward git-tree update to gh-pages."""

    def __init__(self, repo):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
            raise ValueError("invalid Pages repository")
        self.repo = repo

    def api(self, method, endpoint, body=None, missing_ok=False, conflict_ok=False):
        api_path = "repos/" + self.repo + ("/" + endpoint if endpoint else "")
        args = ["gh", "api", "-X", method, api_path]
        with tempfile.TemporaryDirectory(prefix="choscordb-pages-api-") as directory:
            if body is not None:
                payload = Path(directory) / "request.json"
                payload.write_text(json.dumps(body))
                args.extend(["--input", str(payload)])
            try:
                result = subprocess.run(
                    args,
                    capture_output=True,
                    timeout=120,
                    env={
                        **os.environ,
                        "GH_HOST": "github.com",
                        "GH_PROMPT_DISABLED": "1",
                    },
                )
            except (OSError, subprocess.TimeoutExpired):
                raise ValueError("GitHub Pages request failed") from None
        if result.returncode:
            if missing_ok and b"HTTP 404" in result.stderr:
                return None
            if conflict_ok and b"HTTP 409" in result.stderr:
                return {"_conflict": True}
            raise ValueError(
                "GitHub Pages request failed; inspect authentication, permissions or remote state"
            )
        if not result.stdout:
            return {}
        try:
            return json.loads(result.stdout)
        except (ValueError, UnicodeError):
            raise ValueError("invalid GitHub Pages response") from None

    def fetch(self, url):
        request = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                if response.url != url or response.status != 200:
                    raise ValueError(
                        "Pages redirected away from the configured HTTPS host"
                    )
                data = response.read(1024 * 1024 + 1)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise ValueError("public Pages feed is unavailable") from None
        except (OSError, urllib.error.URLError):
            raise ValueError("public Pages feed is unavailable") from None
        if len(data) > 1024 * 1024:
            raise ValueError("public Pages feed is unexpectedly large")
        return data

    @staticmethod
    def _expected_site(page):
        return (
            isinstance(page, dict)
            and page.get("source") == {"branch": "gh-pages", "path": "/"}
            and not page.get("cname")
        )

    def _configure_pages(self):
        result = self.api(
            "POST",
            "pages",
            {"build_type": "legacy", "source": {"branch": "gh-pages", "path": "/"}},
            conflict_ok=True,
        )
        if result.get("_conflict") and not self._expected_site(
            self.api("GET", "pages")
        ):
            raise ValueError("conflicting Pages site appeared during bootstrap")

    def publish(self, repo, files):
        if (
            repo != self.repo
            or not files
            or set(files)
            - {"choscordb-appcast.xml", "windows-x64.json", "linux-x86_64.json"}
        ):
            raise ValueError("unexpected Pages repository or file set")
        page = self.api("GET", "pages", missing_ok=True)
        if page is not None and not self._expected_site(page):
            raise ValueError(
                "Pages source or custom domain conflicts with canonical feed URLs"
            )
        ref = self.api("GET", "git/ref/heads/gh-pages", missing_ok=True)
        if ref is None:
            repository = self.api("GET", "")
            default_branch = repository["default_branch"]
            parent = self.api("GET", "git/ref/heads/" + default_branch)["object"]["sha"]
            base_tree = None
        else:
            parent = ref["object"]["sha"]
            base_tree = self.api("GET", "git/commits/" + parent)["tree"]["sha"]
            old_tree = self.api("GET", "git/trees/" + base_tree + "?recursive=1")
            if old_tree.get("truncated"):
                raise ValueError("Pages tree inventory is truncated")
            old = {
                item["path"]: item["sha"]
                for item in old_tree.get("tree", [])
                if item.get("type") == "blob"
            }
            if all(
                old.get("updates/" + name)
                == hashlib.sha1(
                    b"blob " + str(len(data)).encode() + b"\0" + data
                ).hexdigest()
                for name, data in files.items()
            ):
                if page is None:
                    self._configure_pages()
                return
        entries = [
            {
                "path": "updates/" + name,
                "mode": "100644",
                "type": "blob",
                "content": data.decode("utf-8"),
            }
            for name, data in sorted(files.items())
        ]
        tree_body = {"tree": entries}
        if base_tree:
            tree_body["base_tree"] = base_tree
        tree = self.api("POST", "git/trees", tree_body)
        commit = self.api(
            "POST",
            "git/commits",
            {
                "message": "Publish ChoscorDB stable update feeds",
                "tree": tree["sha"],
                "parents": [parent],
            },
        )
        if ref is None:
            self.api(
                "POST", "git/refs", {"ref": "refs/heads/gh-pages", "sha": commit["sha"]}
            )
        else:
            self.api(
                "PATCH",
                "git/refs/heads/gh-pages",
                {"sha": commit["sha"], "force": False},
            )
        if page is None:
            self._configure_pages()


def _wait_for_pages(pages, base, files, timeout=300):
    prefix = feed_url(base).rsplit("/", 1)[0]
    until = time.monotonic() + timeout
    while True:
        if all(
            pages.fetch(prefix + "/" + name) == data for name, data in files.items()
        ):
            return
        if time.monotonic() >= until:
            raise ValueError(
                "Pages deployment has not reached the canonical public URLs"
            )
        time.sleep(10)


def verify_bootstrap_appcast(data, base, expected_version="0.1.7", sparkle_tools=None):
    _, _, versions = parse_feed(data, base)
    if set(versions) != {expected_version}:
        raise ValueError(
            "bootstrap appcast must contain only the expected previous version"
        )
    enclosure = versions[expected_version].find("enclosure")
    size = int(enclosure.get("length"))
    if size <= 0:
        raise ValueError("bootstrap DMG length is invalid")
    url = enclosure.get("url")
    signature = enclosure.get(
        "{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature"
    )
    with tempfile.TemporaryDirectory(prefix="choscordb-bootstrap-") as directory:
        archive = Path(directory) / f"ChoscorDB-{expected_version}.dmg"
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
                "--output",
                str(archive),
                url,
            ],
            capture_output=True,
            timeout=610,
        )
        if result.returncode or not archive.is_file() or archive.stat().st_size != size:
            raise ValueError("bootstrap DMG public bytes or declared length differ")
        tool = resolve_sparkle_tools(sparkle_tools) / "bin/sign_update"
        result = subprocess.run(
            [tool, "--account", ACCOUNT, "--verify", archive, signature],
            capture_output=True,
            timeout=120,
        )
        if result.returncode:
            raise ValueError("bootstrap DMG Sparkle signature verification failed")


def bootstrap(
    base,
    appcast,
    pages,
    dry_run=False,
    expected_version="0.1.7",
    sparkle_tools=None,
    verifier=verify_bootstrap_appcast,
):
    base = release_base(base)
    data = Path(appcast).read_bytes()
    verifier(data, base, expected_version, sparkle_tools)
    existing = pages.fetch(feed_url(base))
    if existing is not None and existing != data:
        raise ValueError("Pages already has a different appcast; refusing rollback")
    files = {"choscordb-appcast.xml": data}
    if not dry_run and existing is None:
        repo = base.removeprefix("https://github.com/").removesuffix("/releases")
        pages.publish(repo, files)
        _wait_for_pages(pages, base, files)
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    first = sub.add_parser(
        "bootstrap", help="install an existing signed appcast on Pages"
    )
    first.add_argument("--repo", required=True)
    first.add_argument("--appcast", type=Path, required=True)
    first.add_argument("--expected-version", default="0.1.7")
    first.add_argument("--sparkle-tools", type=Path)
    site_check = sub.add_parser(
        "verify-site", help="require a live canonical Pages appcast"
    )
    site_check.add_argument("--repo", required=True)
    second = sub.add_parser(
        "deploy", help="sign and publish feeds after a complete Release"
    )
    second.add_argument("--manifest", type=Path, required=True)
    second.add_argument("--windows", type=Path, required=True)
    second.add_argument("--linux", type=Path, required=True)
    second.add_argument("--notes-file", type=Path, required=True)
    second.add_argument("--private-key", type=Path, required=True)
    inspect = sub.add_parser("inspect-key", help="show the dedicated public key only")
    inspect.add_argument("--private-key", type=Path, required=True)
    inspect.add_argument(
        "--allow-unpinned",
        action="store_true",
        help="inspect a new fork key before embedding its public half",
    )
    keygen = sub.add_parser(
        "keygen", help="create a private key without overwriting one"
    )
    keygen.add_argument("--private-key", type=Path, required=True)
    for command in (first, second):
        command.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.command == "bootstrap":
            base = release_base("https://github.com/" + args.repo + "/releases")
            bootstrap(
                base,
                args.appcast,
                GitHubPages(args.repo),
                args.dry_run,
                args.expected_version,
                args.sparkle_tools,
            )
        elif args.command == "verify-site":
            base = release_base("https://github.com/" + args.repo + "/releases")
            print(verify_live_feed(base))
        elif args.command == "inspect-key":
            private = args.private_key.resolve(strict=True)
            if private.stat().st_mode & 0o077:
                raise ValueError("private update key permissions must be 0600")
            key = public_from_private(private)
            if key != PUBLIC_KEY and not args.allow_unpinned:
                raise ValueError("private key differs from the dedicated update key")
            print("key_id=" + KEY_ID)
            print("public_key=" + key)
        elif args.command == "keygen":
            private = args.private_key.expanduser().resolve(strict=False)
            if private.is_relative_to(Path(__file__).resolve().parents[2]):
                raise ValueError("private update key must be outside the repository")
            key = create_private_key(private)
            print("key_id=" + KEY_ID)
            print("public_key=" + key)
        else:
            manifest = verify_manifest(args.manifest.resolve())
            base = release_base(manifest["base_url"])
            repo = base.removeprefix("https://github.com/").removesuffix("/releases")
            private = args.private_key.resolve(strict=True)
            if private.is_relative_to(Path(__file__).resolve().parents[2]):
                raise ValueError("private update key must be outside the repository")
            if private.stat().st_mode & 0o077:
                raise ValueError("private update key permissions must be 0600")
            pages = GitHubPages(repo)
            deploy(
                args.manifest.resolve().parent,
                manifest,
                args.windows.resolve(),
                args.linux.resolve(),
                args.notes_file.read_text(),
                private,
                GitHubStore(repo),
                pages,
                dry_run=args.dry_run,
                wait_seconds=300,
            )
        return 0
    except (ValueError, KeyError) as error:
        print("Feed publication refused: " + str(error))
        return 1
    except (OSError, subprocess.TimeoutExpired):
        print("Feed publication refused: local file, tool, or network operation failed")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
