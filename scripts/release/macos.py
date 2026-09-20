#!/usr/bin/env python3
"""Local macOS production release preparation, packaging and verification."""

import argparse
import base64
import gzip
from datetime import datetime, timezone
import os
import platform
import plistlib
import shutil
import tarfile
import tempfile
import urllib.request
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

import stage
import source_archive
import notices_sbom
import prepare_qt_notices
import cargo_licenses
from release_consistency import verify_release_consistency
from artifact_privacy import verify_payload_privacy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[2]
LOG_DIRECTORY = None


def sanitized(text):
    text = text.replace(str(Path.home()), "<home>")
    text = re.sub(
        r"Developer ID Application:[^\n\"]+",
        "Developer ID Application: <redacted>",
        text,
    )
    text = re.sub(r"[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}", "<email>", text, flags=re.I)
    text = re.sub(
        r"(?i)(password|token|secret|authorization)\s*[:=]\s*\S+",
        r"\1=<redacted>",
        text,
    )
    return text[-32000:]


def run(argv, **kwargs):
    result = subprocess.run(
        list(map(str, argv)), capture_output=True, text=True, timeout=7200, **kwargs
    )
    if LOG_DIRECTORY is not None and result.returncode:
        with (LOG_DIRECTORY / "failures.log").open("a") as log:
            log.write(
                Path(str(argv[0])).name
                + "\n"
                + sanitized(result.stdout + result.stderr)
                + "\n"
            )
    if result.returncode:
        raise ValueError(
            f"{Path(str(argv[0])).name} failed (exit {result.returncode}); "
            + sanitized(result.stderr)
        )
    return result.stdout


def stable(value):
    if not isinstance(value, str) or not re.fullmatch(
        r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value
    ):
        raise ValueError("Release version must be stable X.Y.Z")
    return tuple(map(int, value.split(".")))


def preflight(root, version=None):
    root = Path(root).resolve(strict=True)
    if run(
        ["git", "-C", root, "status", "--porcelain", "--untracked-files=all"]
    ).strip():
        raise ValueError(
            "Production packaging requires a clean tracked and untracked source checkout"
        )
    authority = tomllib.loads((root / "Cargo.toml").read_text())["workspace"][
        "package"
    ]["version"]
    stable(authority)
    if version is not None and version != authority:
        raise ValueError(
            "Requested version does not match authoritative Cargo.toml version"
        )
    changelog = (root / "CHANGELOG.md").read_text()
    if not re.search(
        r"^##\s+\[?" + re.escape(authority) + r"\]?(?:\s|$)", changelog, re.M
    ):
        raise ValueError("CHANGELOG.md must contain a matching release heading")
    return {
        "version": authority,
        "source_commit": run(["git", "-C", root, "rev-parse", "HEAD"]).strip(),
    }


SPARKLE_VERSION = "2.9.6"
SPARKLE_SHA256 = "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192"
SPARKLE_URL = f"https://github.com/sparkle-project/Sparkle/releases/download/{SPARKLE_VERSION}/Sparkle-{SPARKLE_VERSION}.tar.xz"
ACCOUNT = "com.choscor.ChoscorDB"
BASE_URL = "https://github.com/choscor/choscordb/releases"
NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
ET.register_namespace("sparkle", NS)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def download(url, destination, checksum):
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        temporary = destination.with_suffix(".download")
        try:
            with (
                urllib.request.urlopen(url, timeout=120) as response,
                temporary.open("wb") as target,
            ):
                shutil.copyfileobj(response, target)
            if digest(temporary) != checksum:
                raise ValueError("Dependency archive checksum mismatch")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
    if digest(destination) != checksum:
        raise ValueError("Cached dependency archive checksum mismatch")
    return destination


def production_base(value):
    parsed = urlsplit(value)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.query
        or parsed.fragment
        or value.endswith("/")
    ):
        raise ValueError(
            "Production base URL must be HTTPS without credentials, query, fragment or trailing slash"
        )
    return value


def release_base(value):
    """Accept only a public GitHub repository's canonical releases root."""
    parsed = urlsplit(value)
    if (
        parsed.scheme != "https"
        or parsed.netloc != "github.com"
        or parsed.query
        or parsed.fragment
        or not re.fullmatch(r"/[A-Za-z0-9-]+/[A-Za-z0-9_.-]+/releases", parsed.path)
        or parsed.path.split("/")[2] in {".", ".."}
    ):
        raise ValueError(
            "Release base URL must be https://github.com/OWNER/REPO/releases; "
            "rebuild releases configured for a previous host"
        )
    return value


def feed_url(base):
    return release_base(base) + "/latest/download/choscordb-appcast.xml"


def artifact_url(base, version, name):
    stable(version)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", name) or name in {".", ".."}:
        raise ValueError("Invalid release artifact name")
    return release_base(base) + f"/download/v{version}/{name}"


def write_appcast(path, base_url, version, dmg, signature):
    rss = ET.Element("rss", version="2.0")
    channel = ET.SubElement(rss, "channel")
    ET.SubElement(channel, "title").text = "ChoscorDB"
    item = ET.SubElement(channel, "item")
    ET.SubElement(item, "title").text = "ChoscorDB " + version
    ET.SubElement(item, "{" + NS + "}version").text = version
    ET.SubElement(item, "{" + NS + "}shortVersionString").text = version
    ET.SubElement(item, "{" + NS + "}minimumSystemVersion").text = "26.0"
    ET.SubElement(
        item,
        "enclosure",
        {
            "url": artifact_url(base_url, version, dmg.name),
            "length": str(dmg.stat().st_size),
            "type": "application/octet-stream",
            "{" + NS + "}edSignature": signature,
            "{" + NS + "}version": version,
            "{" + NS + "}shortVersionString": version,
        },
    )
    ET.ElementTree(rss).write(path, encoding="utf-8", xml_declaration=True)


def host_tools():
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise ValueError("Production packaging requires Apple Silicon macOS")
    for tool in [
        "cmake",
        "ninja",
        "cargo",
        "rustup",
        "xcrun",
        "codesign",
        "security",
        "hdiutil",
        "ditto",
        "lipo",
        "otool",
        "spctl",
    ]:
        if not shutil.which(tool):
            raise ValueError("Required release tool missing: " + tool)
    run([sys.executable, "-c", "import cairosvg"])
    sdk = run(["xcrun", "--sdk", "macosx", "--show-sdk-version"]).strip()
    if tuple(map(int, sdk.split(".")[:2])) < (26, 0):
        raise ValueError("macOS SDK 26.0 or newer is required")


def prepare(root, dependencies, qt_mirror=None):
    root = Path(root).resolve(strict=True)
    if qt_mirror is not None:
        production_base(qt_mirror)
    host_tools()
    dependencies = Path(dependencies).resolve()
    dependencies.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "MACOSX_DEPLOYMENT_TARGET": "26.0"}
    qt = dependencies / "qt/6.8.3/macos"
    # aqt 3.3.0 verifies every Qt archive against upstream HTTPS checksum metadata.
    if (
        run(
            [
                sys.executable,
                "-c",
                'import importlib.metadata; print(importlib.metadata.version("aqtinstall"))',
            ]
        ).strip()
        != "3.3.0"
    ):
        raise ValueError(
            "Install pinned scripts/ci/requirements.txt (aqtinstall 3.3.0 required)"
        )
    if (qt / "bin/qmake").exists():
        previous = dependencies / "payload-hashes.json"
        if previous.is_symlink() or not previous.is_file():
            raise ValueError(
                "Existing Qt cache lacks verified preparation evidence; remove the Qt prefix and prepare again"
            )
        hashes = json.loads(previous.read_text())
        expected = {
            name: value
            for name, value in hashes.items()
            if name.startswith("qt/6.8.3/macos/")
        }
        actual = {
            p.relative_to(dependencies).as_posix(): digest(p)
            for p in stage.regular_files(qt)
        }
        if not expected or expected != actual:
            raise ValueError(
                "Prepared Qt cache has changed; remove the Qt prefix and prepare again"
            )
    if not (qt / "bin/qmake").exists():
        run(
            [
                sys.executable,
                "-m",
                "aqt",
                "install-qt",
                "mac",
                "desktop",
                "6.8.3",
                "clang_64",
                "--outputdir",
                dependencies / "qt",
                *(["--base", qt_mirror] if qt_mirror else []),
                "--keep",
                "--archive-dest",
                dependencies / "cache/qt",
                "--archives",
                "qtbase",
                "qttools",
                "qtsvg",
            ],
            cwd=dependencies,
            env=env,
        )
    if run([qt / "bin/qmake", "-query", "QT_VERSION"]).strip() != "6.8.3":
        raise ValueError("Prepared Qt version mismatch")
    qsci = dependencies / "qscintilla"
    run(
        [
            sys.executable,
            root / "scripts/ci/bootstrap_qscintilla.py",
            "--qmake",
            qt / "bin/qmake",
            "--prefix",
            qsci,
            "--work",
            dependencies / "qscintilla-source",
        ],
        env=env,
    )
    cache = dependencies / "cache"
    sparkle_archive = download(
        SPARKLE_URL, cache / f"Sparkle-{SPARKLE_VERSION}.tar.xz", SPARKLE_SHA256
    )
    sparkle = dependencies / "sparkle"
    if sparkle.exists():
        shutil.rmtree(sparkle)
    sparkle.mkdir()
    with tarfile.open(sparkle_archive) as archive:
        archive.extractall(sparkle, filter="data")
    for name, url, checksum in [
        ("qtbase", prepare_qt_notices.URL, prepare_qt_notices.SHA256),
        ("qtsvg", prepare_qt_notices.SVG_URL, prepare_qt_notices.SVG_SHA256),
    ]:
        download(url, cache / f"{name}-6.8.3.tar.xz", checksum)
    if (qt / "licenses").exists():
        shutil.rmtree(qt / "licenses")
    prepare_qt_notices.prepare(
        cache / "qtbase-6.8.3.tar.xz", qt / "licenses", cache / "qtsvg-6.8.3.tar.xz"
    )
    record = {
        "Qt": "6.8.3",
        "QScintilla": "2.14.1",
        "Sparkle": SPARKLE_VERSION,
        "Sparkle_sha256": SPARKLE_SHA256,
        "minimum_macos": "26.0",
        "architecture": "arm64",
    }
    save(dependencies / "dependencies.json", record)
    save(
        dependencies / "payload-hashes.json",
        {
            p.relative_to(dependencies).as_posix(): digest(p)
            for tree in [qt, qsci, sparkle]
            for p in stage.regular_files(tree)
        },
    )
    return record


def source_output(root, output):
    source_archive.create_candidate(root, output)
    # The caller proves clean Git state before and after packaging; source candidate
    # stays a truthful working-tree snapshot and is bound to the release commit.
    return output / "manifest.json"


def refresh_stage_manifest(prefix):
    path = prefix / "manifest.json"
    manifest = json.loads(path.read_text())
    manifest["files"] = [
        {
            "path": p.relative_to(prefix).as_posix(),
            "size": p.stat().st_size,
            "sha256": digest(p),
            "executable": bool(p.stat().st_mode & 0o111),
        }
        for p in stage.regular_files(prefix)
        if p.relative_to(prefix).as_posix() not in {"manifest.json", "SHA256SUMS"}
    ]
    save(path, manifest)
    (prefix / "SHA256SUMS").write_text(digest(path) + "  manifest.json\n")


def strip_development_payload(app):
    removals = []
    for path in Path(app).rglob("*"):
        framework_child = any(parent.suffix == ".framework" for parent in path.parents)
        if path.name.endswith(".dSYM") or (
            framework_child and path.name in {"Headers", "PrivateHeaders", "Modules"}
        ):
            removals.append(path)
    for path in sorted(removals, key=lambda item: len(item.parts), reverse=True):
        if path.is_symlink():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)


def thin_and_check(app, thin=False):
    count = 0
    for binary in stage.regular_files(app):
        if not stage.is_macho(binary):
            continue
        count += 1
        arches = run(["lipo", "-archs", binary]).strip().split()
        if thin and "arm64" in arches and len(arches) > 1:
            temporary = binary.with_name(binary.name + ".arm64")
            run(["lipo", binary, "-thin", "arm64", "-output", temporary])
            temporary.replace(binary)
            arches = ["arm64"]
        if arches != ["arm64"]:
            raise ValueError("Bundled Mach-O must contain only arm64: " + binary.name)
        loads = run(["otool", "-l", binary])
        versions = []
        for block in re.split(r"Load command \d+", loads):
            if "cmd LC_BUILD_VERSION" in block:
                if not re.search(r"^\s*platform (?:1|macos)\s*$", block, re.M | re.I):
                    raise ValueError("Bundled Mach-O uses a non-macOS platform")
                versions += re.findall(
                    r"^\s*minos (\d+(?:\.\d+){0,2})\s*$", block, re.M
                )
            elif "cmd LC_VERSION_MIN_MACOSX" in block:
                versions += re.findall(
                    r"^\s*version (\d+(?:\.\d+){0,2})\s*$", block, re.M
                )
        if not versions or any(
            tuple((list(map(int, v.split("."))) + [0, 0])[:3]) > (26, 0, 0)
            for v in versions
        ):
            raise ValueError(
                "Bundled Mach-O minimum OS is missing or exceeds macOS 26.0: "
                + binary.name
            )
    if not count:
        raise ValueError("No Mach-O payload found")
    return count


def check_bundle(app, version, key, feed):
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    expected = {
        "CFBundleIdentifier": ACCOUNT,
        "CFBundleIconFile": "AppIcon.icns",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": "26.0",
        "SUPublicEDKey": key,
        "SUFeedURL": feed,
        "SUAllowsAutomaticUpdates": False,
    }
    for field, value in expected.items():
        if info.get(field) != value:
            raise ValueError("Production bundle metadata mismatch: " + field)
    icon = app / "Contents/Resources/AppIcon.icns"
    if icon.is_symlink() or not icon.is_file():
        raise ValueError("Production application icon is missing or unsafe")
    artwork = icon.read_bytes()
    if (
        len(artwork) <= 8
        or artwork[:4] != b"icns"
        or int.from_bytes(artwork[4:8], "big") != len(artwork)
    ):
        raise ValueError("Production application icon is invalid")
    if len(base64.b64decode(key, validate=True)) != 32:
        raise ValueError("Invalid Sparkle public key")
    return info


def sign(app, identity):
    def code(path):
        run(
            [
                "codesign",
                "--force",
                "--sign",
                identity,
                "--options",
                "runtime",
                "--timestamp",
                path,
            ]
        )

    # Deepest Mach-O first, then each enclosing code bundle, then the application.
    for binary in sorted(
        stage.regular_files(app), key=lambda p: len(p.parts), reverse=True
    ):
        if stage.is_macho(binary):
            code(binary)
    bundles = [
        p
        for p in app.rglob("*")
        if not p.is_symlink()
        and p.is_dir()
        and p.suffix in {".framework", ".app", ".xpc", ".bundle"}
    ]
    for bundle in sorted(bundles, key=lambda p: len(p.parts), reverse=True):
        code(bundle)
    code(app)


def notarize(artifact, profile, logs):
    process = subprocess.run(
        [
            "xcrun",
            "notarytool",
            "submit",
            str(artifact),
            "--keychain-profile",
            profile,
            "--wait",
            "--output-format",
            "json",
        ],
        capture_output=True,
        text=True,
        timeout=7200,
    )
    try:
        result = json.loads(process.stdout)
    except ValueError:
        (logs / (artifact.name + ".notary.log")).write_text(
            sanitized(process.stdout + process.stderr)
        )
        raise ValueError(
            "Notarization did not return valid JSON; see sanitized logs"
        ) from None
    save(
        logs / (artifact.name + ".notary.json"),
        {k: result.get(k) for k in ["id", "status", "message"]},
    )
    if process.returncode or result.get("status") != "Accepted":
        if re.fullmatch(r"[0-9a-fA-F-]{36}", result.get("id", "")):
            diagnostic = subprocess.run(
                [
                    "xcrun",
                    "notarytool",
                    "log",
                    result["id"],
                    "--keychain-profile",
                    profile,
                ],
                capture_output=True,
                text=True,
                timeout=120,
            )
            (logs / (artifact.name + ".notary.log")).write_text(
                sanitized(diagnostic.stdout + diagnostic.stderr)
            )
        raise ValueError("Notarization was not Accepted; see sanitized submission logs")


def assess(app, dmg):
    run(["codesign", "--verify", "--deep", "--strict", app])
    details = subprocess.run(
        ["codesign", "-d", "--verbose=4", str(app)],
        capture_output=True,
        text=True,
        check=True,
    ).stderr
    flags = re.search(r"^CodeDirectory .*\bflags=(0x[0-9a-fA-F]+)\b", details, re.M)
    if (
        not re.search(r"^Authority=Developer ID Application:", details, re.M)
        or flags is None
        or not int(flags[1], 16) & 0x10000
    ):
        raise ValueError("Application requires Developer ID and hardened runtime")
    run(["xcrun", "stapler", "validate", app])
    run(["spctl", "--assess", "--type", "execute", app])
    run(["codesign", "--verify", "--strict", dmg])
    run(["xcrun", "stapler", "validate", dmg])
    run(
        [
            "spctl",
            "--assess",
            "--type",
            "open",
            "--context",
            "context:primary-signature",
            dmg,
        ]
    )


def safe_artifact(root, value):
    if (
        not isinstance(value, str)
        or Path(value).name != value
        or value in {"", ".", ".."}
    ):
        raise ValueError("Artifact path must be a safe basename")
    path = root / value
    if path.is_symlink() or not path.is_file():
        raise ValueError("Artifact missing or unsafe: " + value)
    return path


def write_portable_tar_gz(destination, sources):
    """Export source/license bytes without local owners or filesystem timestamps."""

    def portable(member):
        member.uid = member.gid = 0
        member.uname = member.gname = ""
        member.mtime = 0
        # Do not allow extended headers to override normalized fields. Tarfile
        # recreates required long-name headers from the explicit relative arcname.
        member.pax_headers = {}
        return member

    with Path(destination).open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(
                fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT
            ) as archive:
                for source, name in sources:
                    archive.add(source, arcname=name, filter=portable)


def write_public_manifest(path, manifest):
    if "sparkle_tools" in manifest:
        raise ValueError("Public release manifest must not contain local tool paths")
    save(path, manifest)


def resolve_sparkle_tools(value=None):
    """Resolve maintainer-local tool configuration, never public release metadata."""
    return (
        Path(
            value
            or os.environ.get("CHOSCORDB_SPARKLE_TOOLS")
            or ROOT / "build/release-dependencies/sparkle"
        )
        .expanduser()
        .resolve()
    )


def verify_manifest(path, sparkle_tools=None):
    path = Path(path).resolve(strict=True)
    data = json.loads(path.read_text())
    if "sparkle_tools" in data:
        raise ValueError(
            "Public release manifest must not contain local tool paths; rebuild the release"
        )
    if (
        data.get("format_version") != 1
        or data.get("production") is not True
        or data.get("status") != "verified"
    ):
        raise ValueError("Manifest is not a verified production release")
    stable(data.get("version"))
    version = data["version"]
    if not re.fullmatch("[0-9a-f]{40}", data.get("source_commit", "")):
        raise ValueError("Release source commit is invalid")
    base_url = release_base(data.get("base_url", ""))
    if data.get("feed_url") != feed_url(base_url):
        raise ValueError("Manifest feed configuration is not production")
    artifacts = data.get("artifacts", [])
    seen = set()
    roles = {}
    for entry in artifacts:
        artifact = safe_artifact(path.parent, entry["path"])
        if entry["path"] in seen:
            raise ValueError("Duplicate release artifact")
        seen.add(entry["path"])
        if (
            digest(artifact) != entry["sha256"]
            or artifact.stat().st_size != entry["size"]
        ):
            raise ValueError("Release artifact changed: " + entry["path"])
        roles.setdefault(entry["role"], []).append(artifact)
    for role in ["dmg", "latest", "appcast", "source", "metadata"]:
        if not roles.get(role):
            raise ValueError("Missing release artifact role: " + role)
    canonical = {
        f"ChoscorDB-{version}.dmg": "dmg",
        "ChoscorDB.dmg": "latest",
        "choscordb-appcast.xml": "appcast",
        f"ChoscorDB-{version}-source.tar.gz": "source",
        f"ChoscorDB-{version}-source.json": "metadata",
        f"ChoscorDB-{version}-sbom.spdx.json": "metadata",
        f"ChoscorDB-{version}-THIRD_PARTY_NOTICES.md": "metadata",
        f"ChoscorDB-{version}-licenses.tar.gz": "metadata",
        f"ChoscorDB-{version}-cargo-source.tar.gz": "source",
        f"ChoscorDB-{version}-qtbase-6.8.3.tar.xz": "source",
        f"ChoscorDB-{version}-qtsvg-6.8.3.tar.xz": "source",
        f"ChoscorDB-{version}-QScintilla_src-2.14.1.tar.gz": "source",
        f"ChoscorDB-{version}-SHA256SUMS": "metadata",
    }
    declared = {entry["path"]: entry["role"] for entry in artifacts}
    for name, role in canonical.items():
        if declared.get(name) != role:
            raise ValueError(
                "Required canonical artifact absent or wrong role: " + name
            )
    required = [
        "app_signature",
        "app_notarized",
        "dmg_signature",
        "dmg_notarized",
        "relocation_smoke",
        "payload_inventory",
        "arm64_minimum_os",
    ]
    if any(data.get("verification", {}).get(k) is not True for k in required):
        raise ValueError("Release verification is incomplete")
    verify_release_consistency(path.parent, data)
    dmg = safe_artifact(path.parent, f"ChoscorDB-{version}.dmg")
    latest = safe_artifact(path.parent, "ChoscorDB.dmg")
    if (
        roles["dmg"] != [dmg]
        or roles["latest"] != [latest]
        or digest(dmg) != digest(latest)
    ):
        raise ValueError("Release DMG names or latest alias mismatch")
    if data.get("app_path") != "staged/ChoscorDB.app":
        raise ValueError("Invalid release app path")
    app = path.parent / data["app_path"]
    if app.is_symlink() or not app.resolve().is_relative_to(path.parent):
        raise ValueError("Unsafe release app path")
    verify_payload_privacy(app, [Path.home(), ROOT])
    check_bundle(app, version, data["sparkle_public_key"], data["feed_url"])
    feed = ET.parse(safe_artifact(path.parent, "choscordb-appcast.xml"))
    items = feed.findall("./channel/item")
    if len(items) != 1:
        raise ValueError("Package feed must contain exactly its release")
    enclosure = items[0].find("enclosure")
    if (
        enclosure is None
        or enclosure.get("url") != artifact_url(base_url, version, dmg.name)
        or enclosure.get("length") != str(dmg.stat().st_size)
        or items[0].findtext("{" + NS + "}version") != version
    ):
        raise ValueError("Appcast version, download, or size mismatch")
    signature = enclosure.get("{" + NS + "}edSignature", "")
    if len(base64.b64decode(signature, validate=True)) != 64:
        raise ValueError("Invalid Sparkle update signature")
    # OpenSSL's system build may lack Ed25519; pinned Sparkle tool reads ONLY the
    # explicit app-specific key account and verifies the signature over final bytes.
    tool = resolve_sparkle_tools(sparkle_tools) / "bin/sign_update"
    public = run([tool.parent / "generate_keys", "--account", ACCOUNT, "-p"]).strip()
    if public != data["sparkle_public_key"]:
        raise ValueError("Sparkle Keychain public key mismatch")
    run([tool, "--account", ACCOUNT, "--verify", dmg, signature])
    thin_and_check(app)
    stage.MacOSAdapter().validate(app.parent)
    assess(app, dmg)
    sbom = safe_artifact(path.parent, f"ChoscorDB-{version}-sbom.spdx.json")
    inventory = json.loads(sbom.read_text())
    expected = {
        entry["fileName"].removeprefix("./"): entry["checksums"][0]["checksumValue"]
        for entry in inventory["files"]
    }
    actual = {
        p.relative_to(app.parent).as_posix(): digest(p)
        for p in stage.regular_files(app)
    }
    if actual != expected:
        raise ValueError("Final app does not match the software inventory")
    with tempfile.TemporaryDirectory(prefix="choscordb-dmg-verify-") as mount:
        run(["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint", mount, dmg])
        try:
            mounted = (Path(mount) / "ChoscorDB.app").resolve()
            applications = Path(mount) / "Applications"
            if not applications.is_symlink() or applications.readlink() != Path(
                "/Applications"
            ):
                raise ValueError("DMG is missing its Applications shortcut")
            mounted_hashes = {
                p.relative_to(mounted).as_posix(): digest(p)
                for p in stage.regular_files(mounted)
            }
            staged_hashes = {
                p.relative_to(app).as_posix(): digest(p)
                for p in stage.regular_files(app)
            }
            if mounted_hashes != staged_hashes:
                raise ValueError("DMG app differs from the verified release payload")
            check_bundle(mounted, version, data["sparkle_public_key"], data["feed_url"])
            assess(mounted, dmg)
        finally:
            run(["hdiutil", "detach", mount])
    return data


def package(args):
    root = args.root.resolve()
    base_url = release_base(args.base_url)
    record = preflight(root, args.version)
    host_tools()
    dependencies = args.dependencies.resolve(strict=True)
    dep_record = json.loads((dependencies / "dependencies.json").read_text())
    if dep_record != {
        "Qt": "6.8.3",
        "QScintilla": "2.14.1",
        "Sparkle": SPARKLE_VERSION,
        "Sparkle_sha256": SPARKLE_SHA256,
        "minimum_macos": "26.0",
        "architecture": "arm64",
    }:
        raise ValueError(
            "Run prepare: dependency versions do not match production pins"
        )
    hashes = json.loads((dependencies / "payload-hashes.json").read_text())
    actual = {
        p.relative_to(dependencies).as_posix(): digest(p)
        for tree in [
            dependencies / "qt/6.8.3/macos",
            dependencies / "qscintilla",
            dependencies / "sparkle",
        ]
        for p in stage.regular_files(tree)
    }
    if hashes != actual:
        raise ValueError("Prepared dependency cache has changed; run prepare again")
    sparkle = dependencies / "sparkle"
    public = run([sparkle / "bin/generate_keys", "--account", ACCOUNT, "-p"]).strip()
    if len(base64.b64decode(public, validate=True)) != 32:
        raise ValueError("Set up the dedicated ChoscorDB Sparkle Keychain account")
    identities = run(["security", "find-identity", "-v", "-p", "codesigning"])
    matches = re.findall(
        r'\b([A-F0-9]{40}) "Developer ID Application:[^"]+"', identities
    )
    identity = args.identity
    if identity is None:
        if len(matches) != 1:
            raise ValueError(
                "Developer ID identity absent or ambiguous; specify --identity certificate SHA-1"
            )
        identity = matches[0]
    if identity not in matches:
        raise ValueError("Requested Developer ID identity unavailable")
    run(
        [
            "xcrun",
            "notarytool",
            "history",
            "--keychain-profile",
            args.notary_profile,
            "--output-format",
            "json",
        ]
    )
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Release output must be a new directory")
    if not output.is_relative_to(root / "build"):
        raise ValueError("Release outputs must be beneath ignored build/")
    output.mkdir(parents=True)
    logs = output / "logs"
    logs.mkdir()
    global LOG_DIRECTORY
    LOG_DIRECTORY = logs
    save(output / "INCOMPLETE.json", record)
    version = record["version"]
    env = {
        **os.environ,
        "MACOSX_DEPLOYMENT_TARGET": "26.0",
        "CARGO_BUILD_TARGET": "aarch64-apple-darwin",
    }
    qt = dependencies / "qt/6.8.3/macos"
    qsci = dependencies / "qscintilla"
    build = output / "build"
    source = output / "source"
    source_manifest = source_output(root, source)
    run(
        [
            "cmake",
            "-S",
            root,
            "-B",
            build,
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_TESTING=OFF",
            f"-DPython3_EXECUTABLE={sys.executable}",
            "-DCHOSCORDB_PRODUCTION_RELEASE=ON",
            f"-DCHOSCORDB_SPARKLE_ROOT={sparkle}",
            f"-DCHOSCORDB_SPARKLE_PUBLIC_KEY={public}",
            f"-DCHOSCORDB_SPARKLE_FEED_URL={feed_url(base_url)}",
            "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            f"-DCMAKE_PREFIX_PATH={qt};{qsci}",
        ],
        env=env,
    )
    run(["cmake", "--build", build, "--config", "Release"], env=env)
    prefix = output / "staged"
    stage.create_stage(build, prefix, qt / "bin", qsci, source_manifest)
    installed = list(prefix.glob("*.app"))
    app = prefix / "ChoscorDB.app"
    if installed[0] != app:
        installed[0].rename(app)
    framework = app / "Contents/Frameworks/Sparkle.framework"
    if not framework.exists():
        shutil.copytree(sparkle / "Sparkle.framework", framework, symlinks=True)
    notices = app / "Contents/Resources/licenses/Sparkle"
    notices.mkdir(parents=True, exist_ok=True)
    shutil.copy2(sparkle / "LICENSE", notices / "LICENSE")
    save(
        notices / "source.json",
        {
            "version": SPARKLE_VERSION,
            "url": SPARKLE_URL,
            "sha256": SPARKLE_SHA256,
            "license": "MIT",
        },
    )
    strip_development_payload(app)
    thin_and_check(app, thin=True)
    verify_payload_privacy(app, [Path.home(), root])
    check_bundle(app, version, public, feed_url(base_url))
    refresh_stage_manifest(prefix)
    metadata = output / "cargo-metadata.json"
    metadata.write_text(
        run(
            [
                "cargo",
                "metadata",
                "--locked",
                "--format-version",
                "1",
                "--filter-platform",
                "aarch64-apple-darwin",
            ],
            cwd=root,
            env=env,
        )
    )
    cargo_licenses.collect(
        metadata,
        root / "docs/licenses/cargo-fallbacks",
        output / "cargo-licenses",
        "choscordb-bridge",
    )
    created = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    notices_sbom.generate(
        prefix,
        metadata,
        qt / "licenses",
        "6.8.3",
        source_manifest,
        output / "cargo-licenses",
        output / "notices",
        "candidate-" + version,
        created,
        "choscordb-bridge",
        "aarch64-apple-darwin",
    )
    sign(app, identity)
    archive = output / "notarize-app.zip"
    run(["ditto", "-c", "-k", "--keepParent", app, archive])
    notarize(archive, args.notary_profile, logs)
    run(["xcrun", "stapler", "staple", app])
    stage.MacOSAdapter().validate(prefix)
    with tempfile.TemporaryDirectory(prefix="choscordb-release-home-") as home:
        stage.MacOSAdapter().smoke(app / "Contents/MacOS/choscordb", Path(home))
    dmgroot = output / "dmg-root"
    dmgroot.mkdir()
    run(["ditto", app, dmgroot / app.name])
    (dmgroot / "Applications").symlink_to("/Applications")
    dmg = output / f"ChoscorDB-{version}.dmg"
    run(
        [
            "hdiutil",
            "create",
            "-volname",
            "ChoscorDB",
            "-srcfolder",
            dmgroot,
            "-ov",
            "-format",
            "UDZO",
            dmg,
        ]
    )
    run(["codesign", "--sign", identity, "--timestamp", dmg])
    notarize(dmg, args.notary_profile, logs)
    run(["xcrun", "stapler", "staple", dmg])
    assess(app, dmg)
    refresh_stage_manifest(prefix)
    shutil.rmtree(output / "notices")
    notices_sbom.generate(
        prefix,
        metadata,
        qt / "licenses",
        "6.8.3",
        source_manifest,
        output / "cargo-licenses",
        output / "notices",
        "candidate-" + version,
        created,
        "choscordb-bridge",
        "aarch64-apple-darwin",
    )
    shutil.copy2(dmg, output / "ChoscorDB.dmg")
    signature = run(
        [sparkle / "bin/sign_update", "--account", ACCOUNT, "-p", dmg]
    ).strip()
    write_appcast(output / "choscordb-appcast.xml", base_url, version, dmg, signature)
    artifacts = []

    def artifact(path, role):
        artifacts.append(
            {
                "path": path.name,
                "role": role,
                "sha256": digest(path),
                "size": path.stat().st_size,
            }
        )

    for path, role in [
        (dmg, "dmg"),
        (output / "ChoscorDB.dmg", "latest"),
        (output / "choscordb-appcast.xml", "appcast"),
    ]:
        artifact(path, role)
    for original, name, role in [
        (
            source / "choscordb-source.tar.gz",
            f"ChoscorDB-{version}-source.tar.gz",
            "source",
        ),
        (source_manifest, f"ChoscorDB-{version}-source.json", "metadata"),
        (
            output / "notices/sbom.spdx.json",
            f"ChoscorDB-{version}-sbom.spdx.json",
            "metadata",
        ),
        (
            output / "notices/THIRD_PARTY_NOTICES.md",
            f"ChoscorDB-{version}-THIRD_PARTY_NOTICES.md",
            "metadata",
        ),
    ]:
        destination = output / name
        shutil.copy2(original, destination)
        artifact(destination, role)
    vendor = output / "vendor"
    vendor_config = run(["cargo", "vendor", "--locked", vendor], cwd=root, env=env)
    (output / "vendor-config.toml").write_text(
        vendor_config.replace(str(vendor), "vendor")
    )
    vendor_archive = output / f"ChoscorDB-{version}-cargo-source.tar.gz"
    write_portable_tar_gz(
        vendor_archive,
        [(vendor, "vendor"), (output / "vendor-config.toml", ".cargo/config.toml")],
    )
    artifact(vendor_archive, "source")
    license_archive = output / f"ChoscorDB-{version}-licenses.tar.gz"
    write_portable_tar_gz(license_archive, [(output / "notices", "licenses")])
    artifact(license_archive, "metadata")
    # Ship the exact native source archives alongside app sources, including Qt's
    # build-required sources, rather than relying on an upstream URL staying live.
    for original in [
        dependencies / "cache/qtbase-6.8.3.tar.xz",
        dependencies / "cache/qtsvg-6.8.3.tar.xz",
        dependencies / "qscintilla-source/QScintilla_src-2.14.1.tar.gz",
    ]:
        destination = output / f"ChoscorDB-{version}-{original.name}"
        shutil.copy2(original, destination)
        artifact(destination, "source")
    if preflight(root, version) != record:
        raise ValueError("Source changed during packaging")
    verification = {
        k: True
        for k in [
            "app_signature",
            "app_notarized",
            "dmg_signature",
            "dmg_notarized",
            "relocation_smoke",
            "payload_inventory",
            "arm64_minimum_os",
        ]
    }
    verification["macos_26_0_manual"] = "pending"
    sums = output / f"ChoscorDB-{version}-SHA256SUMS"
    sums.write_text(
        "".join(f"{entry['sha256']}  {entry['path']}\n" for entry in artifacts)
    )
    artifact(sums, "metadata")
    manifest = {
        **record,
        "format_version": 1,
        "production": True,
        "status": "verified",
        "base_url": base_url,
        "feed_url": feed_url(base_url),
        "sparkle_public_key": public,
        "app_path": "staged/ChoscorDB.app",
        "dependencies": dep_record,
        "artifacts": artifacts,
        "verification": verification,
    }
    manifest_path = output / f"ChoscorDB-{version}-manifest.json"
    pending_manifest = output / ".manifest-pending.json"
    write_public_manifest(pending_manifest, manifest)
    verify_manifest(pending_manifest, sparkle)
    pending_manifest.replace(manifest_path)
    (output / "SHA256SUMS").write_text(
        "".join(f"{entry['sha256']}  {entry['path']}\n" for entry in artifacts)
        + digest(manifest_path)
        + "  "
        + manifest_path.name
        + "\n"
    )
    (output / "INCOMPLETE.json").unlink()
    return {"manifest": str(manifest_path), "macos_26_0_manual": "pending"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ["preflight", "prepare", "package"]:
        p = sub.add_parser(command)
        p.add_argument("--root", type=Path, default=ROOT)
        if command == "prepare":
            p.add_argument(
                "--qt-mirror",
                help="HTTPS Qt mirror base; checksums still come from aqt trusted upstream",
            )
        if command != "prepare":
            p.add_argument("--version")
        if command != "preflight":
            p.add_argument(
                "--dependencies", type=Path, default=ROOT / "build/release-dependencies"
            )
        if command == "package":
            p.add_argument("--output", required=True, type=Path)
            p.add_argument(
                "--base-url",
                default=os.environ.get("CHOSCORDB_RELEASE_BASE_URL", BASE_URL),
            )
            p.add_argument(
                "--identity", default=os.environ.get("CHOSCORDB_SIGNING_IDENTITY")
            )
            p.add_argument(
                "--notary-profile",
                default=os.environ.get("CHOSCORDB_NOTARY_PROFILE", "agents-notary"),
            )
    p = sub.add_parser("verify")
    p.add_argument("--manifest", required=True, type=Path)
    p.add_argument(
        "--sparkle-tools",
        type=Path,
        help="Local Sparkle tool root; defaults to CHOSCORDB_SPARKLE_TOOLS or build/release-dependencies/sparkle",
    )
    args = parser.parse_args()
    try:
        if args.command == "preflight":
            result = preflight(args.root, args.version)
        elif args.command == "prepare":
            result = prepare(args.root, args.dependencies, args.qt_mirror)
        elif args.command == "package":
            result = package(args)
        else:
            result = verify_manifest(args.manifest, args.sparkle_tools)
        print(json.dumps(result, indent=2))
    except (
        ValueError,
        OSError,
        KeyError,
        TypeError,
        subprocess.SubprocessError,
        ET.ParseError,
    ) as error:
        print(f"Release rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
