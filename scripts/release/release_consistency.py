"""Cross-artifact semantic validation for a local production release.

Archives are inspected as streams and are never extracted.
"""

import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import tarfile
import tomllib

import prepare_qt_notices

DEPENDENCIES = {
    "Qt": "6.8.3",
    "QScintilla": "2.14.1",
    "Sparkle": "2.9.6",
    "Sparkle_sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
    "minimum_macos": "26.0",
    "architecture": "arm64",
}


QSCINTILLA_SHA256 = "dfe13c6acc9d85dfcba76ccc8061e71a223957a6c02f3c343b30a9d43a4cdd4d"
NATIVE_SOURCE_HASHES = {
    "qtbase-6.8.3.tar.xz": prepare_qt_notices.SHA256,
    "qtsvg-6.8.3.tar.xz": prepare_qt_notices.SVG_SHA256,
    "QScintilla_src-2.14.1.tar.gz": QSCINTILLA_SHA256,
}


def verify_release_consistency(root, manifest):
    """Validate source, version, checksum inventory, and bundled dependency metadata."""
    root = Path(root)
    version = manifest["version"]
    source = json.loads((root / f"ChoscorDB-{version}-source.json").read_text())
    if (
        source.get("format_version") != 1
        or source.get("git_revision") != manifest["source_commit"]
    ):
        raise ValueError("Release source commit does not match source provenance")
    expected = {}
    for entry in source["files"]:
        name = entry["path"]
        path = PurePosixPath(name)
        if (
            not name
            or path.is_absolute()
            or ".." in path.parts
            or str(path) != name
            or name in expected
            or type(entry["size"]) is not int
            or entry["size"] < 0
            or not re.fullmatch("[0-9a-f]{64}", entry["sha256"])
        ):
            raise ValueError("Invalid source archive provenance entry")
        expected[name] = entry
    observed = set()
    metadata = {}
    try:
        with tarfile.open(
            root / f"ChoscorDB-{version}-source.tar.gz", "r:gz"
        ) as archive:
            for member in archive:
                name = member.name.removeprefix("choscordb-source/")
                if (
                    not member.name.startswith("choscordb-source/")
                    or not member.isfile()
                    or name not in expected
                    or name in observed
                    or member.size != expected[name]["size"]
                ):
                    raise ValueError(
                        "Release source archive inventory differs from provenance"
                    )
                observed.add(name)
                with archive.extractfile(member) as stream:
                    if name in ("Cargo.toml", "CHANGELOG.md"):
                        data = stream.read()
                        metadata[name] = data
                        digest = hashlib.sha256(data).hexdigest()
                    else:
                        digest = hashlib.file_digest(stream, "sha256").hexdigest()
                if digest != expected[name]["sha256"]:
                    raise ValueError(
                        "Release source archive content differs from provenance"
                    )
    except tarfile.TarError as error:
        raise ValueError("Invalid release source archive") from error
    if observed != set(expected) or not {"Cargo.toml", "CHANGELOG.md"}.issubset(
        metadata
    ):
        raise ValueError("Release source archive is incomplete")
    authority = tomllib.loads(metadata["Cargo.toml"].decode())["workspace"]["package"][
        "version"
    ]
    if authority != version:
        raise ValueError("Release source version does not match release manifest")
    changelog = metadata["CHANGELOG.md"].decode()
    if not re.search(
        r"^##\s+\[?" + re.escape(version) + r"\]?(?:\s|$)", changelog, re.M
    ):
        raise ValueError("Release source changelog has no matching version")

    checksum_name = f"ChoscorDB-{version}-SHA256SUMS"
    declared = {}
    for line in (root / checksum_name).read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/]+)", line)
        if match is None or match[2] in declared:
            raise ValueError("Invalid release checksum inventory")
        declared[match[2]] = match[1]
    hashes = {
        entry["path"]: entry["sha256"]
        for entry in manifest["artifacts"]
        if entry["path"] != checksum_name
    }
    if declared != hashes:
        raise ValueError("Release checksum inventory does not match artifacts")

    if manifest.get("dependencies") != DEPENDENCIES:
        raise ValueError(
            "Release dependency pins do not match supported production dependencies"
        )
    # The caller already validates every artifact's actual bytes against hashes.
    # Bind the native source receipts to upstream pins, not merely to each other.
    for suffix, expected_hash in NATIVE_SOURCE_HASHES.items():
        if hashes.get(f"ChoscorDB-{version}-{suffix}") != expected_hash:
            raise ValueError(
                "Release native source does not match reviewed pin: " + suffix
            )
    app = root / manifest["app_path"]
    frameworks = app / "Contents/Frameworks"
    qt = list(frameworks.glob("Qt*.framework"))
    if not qt:
        raise ValueError("Release Qt framework metadata missing")
    for framework in qt:
        info = plistlib.loads((framework / "Resources/Info.plist").read_bytes())
        if info.get("CFBundleVersion") != DEPENDENCIES["Qt"]:
            raise ValueError(
                "Release Qt framework version differs from dependency pins"
            )
    sparkle = plistlib.loads(
        (frameworks / "Sparkle.framework/Resources/Info.plist").read_bytes()
    )
    if sparkle.get("CFBundleShortVersionString") != DEPENDENCIES["Sparkle"]:
        raise ValueError(
            "Release Sparkle framework version differs from dependency pins"
        )
    notices = app / "Contents/Resources/licenses"
    qsci = json.loads((notices / "QScintilla/source.json").read_text())
    if (
        qsci.get("version") != DEPENDENCIES["QScintilla"]
        or qsci.get("qt_version") != DEPENDENCIES["Qt"]
        or qsci.get("sha256") != QSCINTILLA_SHA256
    ):
        raise ValueError("Release QScintilla provenance differs from dependency pins")
    sparkle_source = json.loads((notices / "Sparkle/source.json").read_text())
    if (
        sparkle_source.get("version") != DEPENDENCIES["Sparkle"]
        or sparkle_source.get("sha256") != DEPENDENCIES["Sparkle_sha256"]
    ):
        raise ValueError("Release Sparkle provenance differs from dependency pins")
