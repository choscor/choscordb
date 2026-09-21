#!/usr/bin/env python3
"""Generate local, deterministic notices and SPDX metadata; never publish."""

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import shutil
import uuid
from prepare_qt_notices import SHA256 as QTBASE_SHA256, SVG_SHA256 as QTSVG_SHA256

QT_SOURCE_HASHES = {"qtbase": QTBASE_SHA256, "qtsvg": QTSVG_SHA256}
LUCIDE_SOURCE_SHA256 = (
    "bb49ca3887278f64979fdb164081fb0dd72c375374cb15060dff8f0e14151e00"
)

GEIST_SOURCE_SHA256 = "1190ba834ead1873d41bbe2210659a927e1eef2ae252ed8d995fe05e17e476a6"

SHADCN_SOURCE_SHA256 = (
    "8e2b3b92fdc4aa4a8b9b81b372ced0cd2a785e25365d157ef450b0773d44f81f"
)

DATABASE_LOGOS_SOURCE_SHA256 = (
    "cbc41e1971859e0ef6549c900a5c97a90deea690fcbad7e649a08947da955f90"
)

QT_PLUGINS = {
    "platforms/libqcocoa.dylib",
    "styles/libqmacstyle.dylib",
    "imageformats/libqgif.dylib",
    "imageformats/libqico.dylib",
    "imageformats/libqjpeg.dylib",
    "imageformats/libqsvg.dylib",
    "iconengines/libqsvgicon.dylib",
}


def checked_texts(root, entries):
    expected = {}
    for entry in entries:
        if entry["path"] in expected:
            raise ValueError("Duplicate license record")
        expected[entry["path"]] = entry["sha256"]
    actual = text_files(root)
    if {name: digest(data) for name, data in actual.items()} != expected:
        raise ValueError("License text hashes or copy list mismatch")
    return actual


def digest(data):
    return hashlib.sha256(data).hexdigest()


def relative(value):
    path = PurePosixPath(value)
    if (
        not value
        or path.is_absolute()
        or ".." in path.parts
        or "\\" in value
        or ":" in value
        or any(ord(c) < 32 for c in value)
    ):
        raise ValueError("Unsafe relative input path")
    return path


def read_local(root, name):
    path = root / relative(name)
    if path.is_symlink() or not path.resolve(strict=True).is_relative_to(
        root.resolve()
    ):
        raise ValueError("Input escapes its declared root")
    return path.read_bytes()


def text_files(root):
    if not root.is_dir():
        raise ValueError("License directory is missing")
    files = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError("Symlink in license source")
        if path.is_file() and path.name != "source.json":
            data = read_local(root, path.relative_to(root).as_posix())
            if not data.strip():
                raise ValueError("Empty license text")
            data.decode("utf-8")
            files[path.relative_to(root).as_posix()] = data
    if not files:
        raise ValueError("License texts are missing")
    return files


def identifier(kind, value):
    return "SPDXRef-" + kind + "-" + digest(value.encode())[:24]


def normal_dependency_packages(metadata, root_package):
    by_id = {}
    for row in metadata["packages"]:
        if row["id"] in by_id:
            raise ValueError("Duplicate Cargo package ID")
        by_id[row["id"]] = row
    resolve = metadata.get("resolve")
    if not resolve:
        raise ValueError("Resolved Cargo graph is required")
    nodes = resolve["nodes"]
    if len({node["id"] for node in nodes}) != len(nodes):
        raise ValueError("Duplicate Cargo graph ID")
    workspace = set(metadata["workspace_members"])
    roots = [
        row["id"]
        for row in metadata["packages"]
        if row["name"] == root_package and row["id"] in workspace
    ]
    if len(roots) != 1:
        raise ValueError("Cargo root workspace package is absent or ambiguous")
    graph = {node["id"]: node for node in nodes}
    reachable, pending = set(), [roots[0]]
    while pending:
        current = pending.pop()
        if current in reachable:
            continue
        if current not in graph or current not in by_id:
            raise ValueError("Resolved dependency graph is incomplete")
        reachable.add(current)
        node = graph[current]
        if "deps" not in node:
            raise ValueError("Cargo dependency kinds are required")
        for edge in node["deps"]:
            if any(kind.get("kind") is None for kind in edge["dep_kinds"]):
                pending.append(edge["pkg"])
    packages, coordinates = [], set()
    for node in sorted(nodes, key=lambda node: node["id"]):
        if node["id"] not in by_id:
            raise ValueError("Cargo graph references unknown package")
        if node["id"] in workspace or node["id"] not in reachable:
            continue
        row = by_id[node["id"]]
        coordinate = row["name"] + "@" + row["version"]
        relative(coordinate)
        if coordinate in coordinates:
            raise ValueError("Ambiguous Cargo license sidecar identity")
        coordinates.add(coordinate)
        packages.append(row)
    return packages


def generate(
    stage,
    cargo_metadata,
    qt_notices,
    qt_version,
    source_candidate,
    cargo_licenses,
    output,
    release_id,
    created,
    cargo_root_package,
    cargo_target,
):
    stage, cargo_metadata, qt_notices, source_candidate, cargo_licenses = map(
        Path, (stage, cargo_metadata, qt_notices, source_candidate, cargo_licenses)
    )
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise FileExistsError(output)
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", release_id):
        raise ValueError("Invalid immutable release identifier")
    if not re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ", created):
        raise ValueError("Creation timestamp must be UTC ISO8601")
    datetime.fromisoformat(created.replace("Z", "+00:00"))
    if not re.fullmatch(r"[A-Za-z0-9_-]+", cargo_target):
        raise ValueError("Explicit Cargo release target is required")
    source_bytes = source_candidate.read_bytes()
    source = json.loads(source_bytes)
    if source.get(
        "source_kind"
    ) == "working-tree snapshot" and not release_id.startswith("candidate-"):
        raise ValueError(
            "Working-tree snapshots require a candidate- release identifier"
        )
    manifest_bytes = read_local(stage, "manifest.json")
    if (
        read_local(stage, "SHA256SUMS").decode().strip()
        != digest(manifest_bytes) + "  manifest.json"
    ):
        raise ValueError("Stage manifest checksum mismatch")
    manifest = json.loads(manifest_bytes)
    stage_source = manifest.get("source_candidate", {})
    if (
        stage_source.get("manifest_sha256") != digest(source_bytes)
        or stage_source.get("source_kind") != source.get("source_kind")
        or stage_source.get("git_revision") != source.get("git_revision")
    ):
        raise ValueError("Stage source-candidate relationship is missing or mismatched")
    payload = {}
    for entry in manifest["files"]:
        name = entry["path"]
        if name in payload:
            raise ValueError("Duplicate staged file")
        data = read_local(stage, name)
        if entry["size"] != len(data) or entry["sha256"] != digest(data):
            raise ValueError("Staged file does not match its manifest")
        payload[name] = data
    actual = {
        p.relative_to(stage).as_posix()
        for p in stage.rglob("*")
        if p.is_file()
        and not p.is_symlink()
        and p.relative_to(stage).as_posix() not in {"manifest.json", "SHA256SUMS"}
    }
    if actual != set(payload):
        raise ValueError("Stage manifest does not cover all payload files")
    for p in stage.rglob("*"):
        if p.is_symlink() and (
            p.readlink().is_absolute()
            or not p.resolve(strict=True).is_relative_to(stage.resolve())
        ):
            raise ValueError("Unsafe stage symlink")

    qt_record = json.loads(read_local(qt_notices, "source.json"))
    if qt_record.get("version") != qt_version:
        raise ValueError("Qt notice version mismatch")
    modules = qt_record.get("modules", {})
    required_modules = {"qtbase"}
    if any("QtSvg.framework/" in name or "/libqsvg" in name for name in payload):
        required_modules.add("qtsvg")
    if not required_modules.issubset(modules):
        raise ValueError("Required Qt source module notices missing")
    qt_entries = []
    for name, module in modules.items():
        if (
            qt_version != "6.8.3"
            or module.get("version") != qt_version
            or module.get("sha256") != QT_SOURCE_HASHES.get(name)
            or name not in QT_SOURCE_HASHES
        ):
            raise ValueError("Invalid Qt module provenance")
        qt_entries.extend(module["copied_files"])
    qt_texts = checked_texts(qt_notices, qt_entries)
    versions = {
        plistlib.loads(data).get("CFBundleVersion")
        for name, data in payload.items()
        if re.search(r"/Qt[^/]+\.framework/", name) and name.endswith("/Info.plist")
    }
    if not versions or versions != {qt_version}:
        raise ValueError("Staged Qt framework versions do not match supplied notices")
    qsci_records = [
        name for name in payload if name.endswith("/licenses/QScintilla/source.json")
    ]
    if len(qsci_records) != 1:
        raise ValueError("Exactly one QScintilla provenance record is required")
    qsci_path = qsci_records[0]
    qsci_record = json.loads(payload[qsci_path])
    app_licenses = [
        name for name in payload if name.endswith("/Resources/licenses/LICENSE")
    ]
    if len(app_licenses) != 1:
        raise ValueError("ChoscorDB license is missing or ambiguous")
    lucide_licenses = [
        name for name in payload if name.endswith("/Resources/licenses/LICENSE-LUCIDE")
    ]
    if len(lucide_licenses) != 1:
        raise ValueError("Lucide icon license is missing or ambiguous")
    lucide_sources = [
        name
        for name in payload
        if name.endswith("/Resources/licenses/SOURCE-LUCIDE.json")
    ]
    if len(lucide_sources) != 1:
        raise ValueError("Lucide icon provenance is missing or ambiguous")
    if digest(payload[lucide_sources[0]]) != LUCIDE_SOURCE_SHA256:
        raise ValueError("Lucide icon provenance does not match the reviewed source")
    lucide_record = json.loads(payload[lucide_sources[0]])
    if digest(payload[lucide_licenses[0]]) != lucide_record["licenseSha256"]:
        raise ValueError("Lucide icon license does not match reviewed source")
    lucide_icons = lucide_record["icons"]
    local_icons = {
        name: record["sha256"]
        for name, record in lucide_record["referenceAdditions"]["icons"].items()
    }
    source_hashes = {entry["path"]: entry["sha256"] for entry in source["files"]}
    for path, expected in [
        ("desktop/resources/icons/SOURCE-LUCIDE.json", LUCIDE_SOURCE_SHA256),
        ("desktop/resources/icons/LICENSE-LUCIDE", lucide_record["licenseSha256"]),
    ]:
        if source_hashes.get(path) != expected:
            raise ValueError(
                "Lucide icon source snapshot does not match reviewed provenance"
            )
    database_sources = [
        name
        for name in payload
        if name.endswith("/Resources/licenses/SOURCE-DATABASE-LOGOS.json")
    ]
    if (
        len(database_sources) != 1
        or digest(payload[database_sources[0]]) != DATABASE_LOGOS_SOURCE_SHA256
        or source_hashes.get("desktop/resources/icons/SOURCE-DATABASE-LOGOS.json")
        != DATABASE_LOGOS_SOURCE_SHA256
    ):
        raise ValueError("Database logo provenance differs from reviewed source")
    database_record = json.loads(payload[database_sources[0]])
    database_icons = {
        name: record["sha256"] for name, record in database_record["icons"].items()
    }
    icon_hashes = {**lucide_icons, **local_icons, **database_icons}
    icon_prefix = app_licenses[0].removesuffix("licenses/LICENSE") + "icons/"
    staged_icons = {icon: icon_prefix + icon for icon in icon_hashes}
    actual_icon_paths = {
        name
        for name in payload
        if "/Resources/icons/" in name and name.endswith((".svg", ".png"))
    }
    if actual_icon_paths != set(staged_icons.values()):
        raise ValueError("Staged icon inventory differs from reviewed assets")
    for icon, expected in icon_hashes.items():
        if (
            digest(payload[staged_icons[icon]]) != expected
            or source_hashes.get("desktop/resources/icons/" + icon) != expected
        ):
            raise ValueError(
                "Icon does not match reviewed adaptation and source: " + icon
            )

    def reviewed_asset(name, expected, label):
        matches = [
            path for path in payload if path.endswith("/Resources/licenses/" + name)
        ]
        if len(matches) != 1 or digest(payload[matches[0]]) != expected:
            raise ValueError(
                label + " notice is missing or differs from reviewed source"
            )
        return matches[0]

    geist_source = reviewed_asset("SOURCE-GEIST.json", GEIST_SOURCE_SHA256, "Geist")
    geist_record = json.loads(payload[geist_source])
    if (
        source_hashes.get("desktop/resources/fonts/SOURCE-GEIST.json")
        != GEIST_SOURCE_SHA256
    ):
        raise ValueError("Geist source snapshot does not match reviewed provenance")
    for entry in geist_record["files"]:
        if (
            source_hashes.get("desktop/resources/fonts/" + entry["local"])
            != entry["sha256"]
        ):
            raise ValueError(
                "Geist embedded font/source hashes do not match reviewed source"
            )
    geist_license = reviewed_asset(
        "OFL.txt",
        "c683bfbcc7e087f5d37a54ef628f10387c451a83ddc459b151403a164ac46c90",
        "Geist",
    )

    shadcn_source = reviewed_asset("SOURCE-SHADCN.json", SHADCN_SOURCE_SHA256, "shadcn")
    shadcn_record = json.loads(payload[shadcn_source])
    shadcn_license = reviewed_asset(
        "LICENSE-SHADCN",
        "1564074e13439397221ffd522e2e504d56561994a23d371aa5e3ad43e4f5423f",
        "shadcn",
    )
    for path, expected in [
        ("docs/licenses/shadcn/manifest.json", SHADCN_SOURCE_SHA256),
        ("docs/licenses/shadcn/LICENSE.md", digest(payload[shadcn_license])),
    ]:
        if source_hashes.get(path) != expected:
            raise ValueError(
                "shadcn attribution/source hashes do not match reviewed source"
            )

    packages, package_ids, license_outputs = [], {}, {}

    def package(key, name, version, license_expression, license_texts, url=None):
        if (
            not isinstance(version, str)
            or not version
            or not isinstance(license_expression, str)
            or not license_expression.strip()
        ):
            raise ValueError("Package version and declared license are required")
        if key in package_ids:
            raise ValueError("Duplicate package identity")
        package_id = identifier("Package", key)
        paths = []
        for path, data in sorted(license_texts.items()):
            destination = "licenses/" + package_id + "/" + str(relative(path))
            license_outputs[destination] = data
            paths.append(destination)
        if not paths:
            raise ValueError("Package license text is missing")
        if url and not url.startswith(("https://", "http://")):
            raise ValueError("Package source must be a public HTTP(S) URL")
        row = {
            "SPDXID": package_id,
            "name": name,
            "versionInfo": version,
            "downloadLocation": url or "NOASSERTION",
            "filesAnalyzed": False,
            "licenseConcluded": "NOASSERTION",
            "licenseDeclared": license_expression,
            "copyrightText": "NOASSERTION",
            "comment": "Included license texts: " + ", ".join(paths),
        }
        packages.append(row)
        package_ids[key] = package_id
        return package_id

    app_id = package(
        "choscordb",
        "ChoscorDB",
        release_id,
        "GPL-3.0-or-later",
        {"LICENSE": payload[app_licenses[0]]},
    )
    packages[-1]["sourceInfo"] = "Source candidate manifest SHA256: " + digest(
        source_bytes
    )
    qt_id = package(
        "qt", "Qt", qt_version, qt_record.get("license"), qt_texts, qt_record.get("url")
    )
    qsci_id = package(
        "qscintilla",
        "QScintilla",
        qsci_record.get("version"),
        qsci_record.get("license"),
        text_files(stage / PurePosixPath(qsci_path).parent),
        qsci_record.get("url"),
    )
    lucide_id = package(
        "lucide-icons",
        "Lucide Icons",
        lucide_record["version"],
        lucide_record["license"],
        {"LICENSE": payload[lucide_licenses[0]]},
        "https://github.com/lucide-icons/lucide",
    )
    packages[-1]["sourceInfo"] = "Reviewed source commit " + lucide_record["commit"]
    geist_id = package(
        "geist",
        "Geist",
        geist_record["revision"],
        "OFL-1.1",
        {"OFL.txt": payload[geist_license]},
        geist_record["repository"],
    )
    packages[-1]["sourceInfo"] = (
        "Embedded Qt font resources; reviewed revision "
        + geist_record["revision"]
        + "; source SHA256: "
        + ", ".join(
            entry["local"] + "=" + entry["sha256"] for entry in geist_record["files"]
        )
    )
    shadcn_id = package(
        "shadcn-ui",
        "shadcn/ui",
        shadcn_record["revision"],
        "MIT",
        {"LICENSE.md": payload[shadcn_license]},
        shadcn_record["repository"],
    )
    packages[-1]["sourceInfo"] = (
        "Native Qt style adaptations of "
        + shadcn_record["preset"]
        + "; reviewed upstream revision "
        + shadcn_record["revision"]
        + "; provenance SHA256 "
        + SHADCN_SOURCE_SHA256
    )
    sparkle_id = None
    sparkle_files = [name for name in payload if "/Sparkle.framework/" in name]
    if sparkle_files:
        records = [
            name for name in payload if name.endswith("/licenses/Sparkle/source.json")
        ]
        if len(records) != 1:
            raise ValueError("Sparkle provenance is missing or ambiguous")
        record = json.loads(payload[records[0]])
        if record != {
            "version": "2.9.6",
            "sha256": "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192",
            "url": "https://github.com/sparkle-project/Sparkle/releases/download/2.9.6/Sparkle-2.9.6.tar.xz",
            "license": "MIT",
        }:
            raise ValueError("Sparkle provenance does not match reviewed pin")
        infos = [
            plistlib.loads(payload[name])
            for name in sparkle_files
            if name.endswith("/Resources/Info.plist")
            and "/XPCServices/" not in name
            and ".app/" not in name.split("Sparkle.framework/", 1)[1]
        ]
        if not infos or any(
            info.get("CFBundleShortVersionString") != "2.9.6" for info in infos
        ):
            raise ValueError("Sparkle framework version does not match notices")
        sparkle_id = package(
            "sparkle",
            "Sparkle",
            record["version"],
            "MIT",
            text_files(stage / PurePosixPath(records[0]).parent),
            record["url"],
        )
    metadata = json.loads(cargo_metadata.read_bytes())
    cargo_ids, proc_macro_ids = [], []
    selected = normal_dependency_packages(metadata, cargo_root_package)
    index = json.loads(read_local(cargo_licenses, "index.json"))
    if index.get("root_package") != cargo_root_package:
        raise ValueError("Cargo license root mismatch")
    records = {}
    for record in index["packages"]:
        key = record["name"] + "@" + record["version"]
        if key in records:
            raise ValueError("Duplicate Cargo license metadata")
        records[key] = record
    if set(records) != {row["name"] + "@" + row["version"] for row in selected}:
        raise ValueError("Cargo license package set mismatch")
    for row in selected:
        coordinate = row["name"] + "@" + row["version"]
        record = records[coordinate]
        if any(
            record.get(key) != row.get(key)
            for key in ["name", "version", "license", "source"]
        ):
            raise ValueError("Cargo license package metadata mismatch")
        package_id = package(
            "cargo:" + row["id"],
            row["name"],
            row["version"],
            row.get("license"),
            checked_texts(cargo_licenses / coordinate, record["files"]),
            row.get("repository") or row.get("homepage"),
        )
        cargo_ids.append(package_id)
        if any(
            "proc-macro" in target.get("kind", []) for target in row.get("targets", [])
        ):
            proc_macro_ids.append(package_id)

    sqlite_relationships = []
    for row in selected:
        if row["name"] != "libsqlite3-sys":
            continue
        node = next(
            node for node in metadata["resolve"]["nodes"] if node["id"] == row["id"]
        )
        features = set(node.get("features", []))
        if "bundled" not in features or any(
            "sqlcipher" in feature for feature in features
        ):
            raise ValueError("SQLite bundled build provenance is absent or unsupported")
        package_root = Path(row.get("manifest_path", "")).parent
        header = read_local(package_root, "sqlite3/sqlite3.h")
        amalgamation = read_local(package_root, "sqlite3/sqlite3.c")

        def define(data, name, pattern):
            matches = re.findall(
                rb"^#define\s+" + name.encode() + rb'\s+"(' + pattern + rb')"',
                data,
                re.MULTILINE,
            )
            if len(set(matches)) != 1:
                raise ValueError("SQLite native version/source provenance is invalid")
            return matches[0].decode()

        version = define(header, "SQLITE_VERSION", rb"\d+\.\d+\.\d+")
        source_id = define(
            header, "SQLITE_SOURCE_ID", rb"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d [0-9a-f]{64}"
        )
        if (
            define(amalgamation, "SQLITE_VERSION", rb"\d+\.\d+\.\d+") != version
            or define(
                amalgamation,
                "SQLITE_SOURCE_ID",
                rb"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d [0-9a-f]{64}",
            )
            != source_id
        ):
            raise ValueError("SQLite header/amalgamation provenance mismatch")
        blessing = re.search(
            rb"The author disclaims copyright.*?May you share freely, never taking more than you give\.",
            amalgamation,
            re.DOTALL,
        )
        if (
            not blessing
            or b"May you do good and not evil." not in blessing[0]
            or b"May you find forgiveness for yourself and forgive others."
            not in blessing[0]
        ):
            raise ValueError("SQLite blessing license is missing")
        license_text = re.sub(rb"\n\*\* ?", b"\n", blessing[0]) + b"\n"
        sqlite_id = package(
            "native:sqlite:" + source_id,
            "SQLite",
            version,
            "blessing",
            {"LICENSE": license_text},
            "https://www.sqlite.org/src/info/" + source_id.split()[-1],
        )
        packages[-1]["sourceInfo"] = (
            "Bundled libsqlite3-sys source; SQLITE_SOURCE_ID "
            + source_id
            + "; sqlite3.h SHA256 "
            + digest(header)
            + "; sqlite3.c SHA256 "
            + digest(amalgamation)
        )
        sqlite_relationships += [
            {
                "spdxElementId": app_id,
                "relationshipType": "STATIC_LINK",
                "relatedSpdxElement": sqlite_id,
            },
            {
                "spdxElementId": package_ids["cargo:" + row["id"]],
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": sqlite_id,
            },
        ]

    files, relationships = (
        [],
        [
            {
                "spdxElementId": "SPDXRef-DOCUMENT",
                "relationshipType": "DESCRIBES",
                "relatedSpdxElement": app_id,
            }
        ],
    )
    relationships.extend(sqlite_relationships)
    for package_id in [
        qt_id,
        qsci_id,
        lucide_id,
        geist_id,
        shadcn_id,
        *([sparkle_id] if sparkle_id else []),
        *cargo_ids,
    ]:
        relationships.append(
            {
                "spdxElementId": app_id,
                "relationshipType": "DEPENDS_ON",
                "relatedSpdxElement": package_id,
            }
        )
    for package_id in cargo_ids:
        if package_id in proc_macro_ids:
            relationships.append(
                {
                    "spdxElementId": package_id,
                    "relationshipType": "BUILD_DEPENDENCY_OF",
                    "relatedSpdxElement": app_id,
                }
            )
        else:
            relationships.append(
                {
                    "spdxElementId": app_id,
                    "relationshipType": "STATIC_LINK",
                    "relatedSpdxElement": package_id,
                }
            )
    for name, data in sorted(payload.items()):
        file_id = identifier("File", name)
        if "/PlugIns/" in name and name.split("/PlugIns/", 1)[1] not in QT_PLUGINS:
            raise ValueError("Unknown Qt plugin ownership: " + name)
        if name.endswith(
            (
                "/Resources/licenses/LICENSE-LUCIDE",
                "/Resources/licenses/SOURCE-LUCIDE.json",
            )
        ) or name in {staged_icons[icon] for icon in lucide_icons}:
            owner = lucide_id
        elif name in {shadcn_source, shadcn_license}:
            owner = shadcn_id
        elif name in {geist_source, geist_license}:
            owner = geist_id
        elif "/Sparkle.framework/" in name or "/licenses/Sparkle/" in name:
            if sparkle_id is None:
                raise ValueError("Sparkle notice has no matching runtime")
            owner = sparkle_id
        elif "qscintilla" in name.lower():
            owner = qsci_id
        elif (
            re.search(r"/Qt[^/]+\.framework/", name)
            or "/PlugIns/" in name
            or name.endswith("/qt.conf")
            or "/licenses/Qt/" in name
        ):
            owner = qt_id
        elif "/Frameworks/" in name or re.search(
            r"\.(dylib|dll|so)(\.|$)", name, re.IGNORECASE
        ):
            raise ValueError("Unknown third-party runtime ownership: " + name)
        else:
            owner = app_id
        files.append(
            {
                "SPDXID": file_id,
                "fileName": "./" + name,
                "checksums": [{"algorithm": "SHA256", "checksumValue": digest(data)}],
                "licenseConcluded": "NOASSERTION",
                "licenseInfoInFiles": ["NOASSERTION"],
                "copyrightText": "NOASSERTION",
            }
        )
        relationships.append(
            {
                "spdxElementId": owner,
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": file_id,
            }
        )
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "ChoscorDB " + release_id,
        "documentNamespace": "urn:uuid:"
        + str(
            uuid.uuid5(uuid.NAMESPACE_URL, release_id + ":" + digest(manifest_bytes))
        ),
        "creationInfo": {
            "created": created,
            "creators": ["Tool: choscordb-notices-sbom"],
        },
        "documentComment": "Operating-system libraries are external prerequisites. Cargo normal-dependency closure rooted at "
        + cargo_root_package
        + "; caller supplied --filter-platform metadata for "
        + cargo_target
        + ". Dev/build-only and unreachable packages excluded.",
        "packages": sorted(packages, key=lambda p: p["SPDXID"]),
        "files": files,
        "relationships": sorted(
            relationships,
            key=lambda r: (
                r["spdxElementId"],
                r["relationshipType"],
                r["relatedSpdxElement"],
            ),
        ),
    }
    notices = [
        "# Third-party notices",
        "",
        "Release identifier: " + release_id,
        "",
        "Operating-system libraries are external prerequisites. Cargo entries cover normal dependencies of "
        + cargo_root_package
        + " for "
        + cargo_target
        + ".",
        "",
    ]
    for row in sorted(packages, key=lambda p: (p["name"], p["versionInfo"])):
        if row["SPDXID"] == app_id:
            continue
        notices += [
            "## " + row["name"] + " " + row["versionInfo"],
            "",
            "Declared license: " + row["licenseDeclared"],
            "",
            "Source: " + row["downloadLocation"],
            "",
            row["comment"],
            "",
        ]
    notices += ["## Database identity artwork", ""]
    for filename, record in sorted(database_record["icons"].items()):
        notices += [
            filename + ": " + record["adaptation"],
            "",
            "Source: " + record["url"],
            "",
        ]
    artifacts = {
        **license_outputs,
        "THIRD_PARTY_NOTICES.md": "\n".join(notices).encode(),
        "sbom.spdx.json": (
            json.dumps(document, indent=2, sort_keys=True) + "\n"
        ).encode(),
    }
    output.mkdir(parents=True, exist_ok=False)
    try:
        for name, data in sorted(artifacts.items()):
            destination = output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        (output / "SHA256SUMS").write_text(
            "".join(
                digest(data) + "  " + name + "\n"
                for name, data in sorted(artifacts.items())
            )
        )
    except BaseException:
        shutil.rmtree(output)
        raise
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in [
        "stage",
        "cargo-metadata",
        "qt-notices",
        "source-candidate",
        "cargo-licenses",
        "output",
    ]:
        parser.add_argument("--" + name, required=True, type=Path)
    for name in [
        "qt-version",
        "release-id",
        "created",
        "cargo-root-package",
        "cargo-target",
    ]:
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    generate(
        args.stage,
        args.cargo_metadata,
        args.qt_notices,
        args.qt_version,
        args.source_candidate,
        args.cargo_licenses,
        args.output,
        args.release_id,
        args.created,
        args.cargo_root_package,
        args.cargo_target,
    )


if __name__ == "__main__":
    main()
