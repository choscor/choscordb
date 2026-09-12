# Dependency notices and SBOM

Status: deferred on 2026-09-12 when the user moved all distribution work to a later iteration.

This contract defines release metadata for PRD §17 when distribution work resumes. It covers the exact staged application and resolved Rust dependency graph; it does not replace legal review.

The generator consumes a verified stage manifest, `cargo metadata --locked`, the workspace license, QScintilla's pinned `source.json` and license, and an explicit Qt source-notice directory matching the staged Qt version. It fails if a staged third-party runtime has no matching package record or required license source. Inputs use local files only during generation; no dependency metadata is inferred from binary names without version evidence.

Outputs are deterministic UTF-8 files: `THIRD_PARTY_NOTICES.md` and an SPDX 2.3 JSON document. Notices list each shipped or statically linked third-party package once with name, version, declared license expression, source/homepage when available, and the included license-text paths. Cargo packages come from the locked resolved graph, excluding workspace packages. Qt modules/plugins are represented by their common Qt source package and enumerate the staged module/plugin files. QScintilla is represented separately. The application package records GPL-3.0-or-later and its source-candidate relationship.

SPDX package and file identifiers are stable, collision-resistant and independent of absolute paths. Every staged payload regular file appears with the SHA-256 from the stage manifest and belongs to exactly one package. Relationships identify application dependency packages and package-to-file containment. The document namespace includes a caller-supplied immutable release identifier; a working-tree candidate uses an explicit non-release identifier and cannot masquerade as a published version. Creation timestamps are caller-supplied or normalized for reproducible candidates.

Validation independently checks SPDX schema-level required fields used here, unique IDs, every file hash against the stage, complete one-to-one file ownership, declared-license presence, included notice files, no absolute/workspace paths, deterministic repeated output and checksums. Unknown, missing or ambiguous ownership fails closed.

Qt's binary SDK may omit its license directory. In that case the stage and SBOM remain incomplete until matching official Qt source notices are supplied and verified. Platform system libraries supplied by the operating system are documented as external prerequisites and are not copied into the package. Unsigned macOS package metadata remains a separate release gate. Windows/Linux delivery and signing/notarization are excluded from the active goal.
