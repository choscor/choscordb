# Unsigned macOS package

Status: deferred on 2026-09-12 when the user moved all distribution work to a later iteration.

This contract defines the remaining macOS package boundary when distribution work resumes.

The packager consumes a verified relocated macOS stage, its bound source-candidate manifest, and verified notices/SBOM output. It embeds `THIRD_PARTY_NOTICES.md`, the SPDX document and their copied license texts under the application Resources directory without changing executable code. It then creates a reproducible ZIP containing one top-level `ChoscorDB.app` bundle with normalized timestamps and ownership-independent permissions. Internal framework symlinks are preserved as relative links; absolute or escaping links fail.

A package manifest records the ZIP SHA-256, uncompressed entry paths, sizes, hashes, modes and symlink targets, plus the source-candidate and stage-manifest hashes. `SHA256SUMS` covers the ZIP, package manifest, source archive and source manifest. Existing outputs are never overwritten.

Verification independently checks ZIP paths before extraction, entry hashes/modes/links, every executable dependency, embedded notices/SBOM checksums and provenance, then extracts into a new temporary directory and launches the application using isolated metadata storage with development library paths removed. The source archive is independently verified against its manifest. Failure leaves no candidate output.

The output is explicitly unsigned. It is not notarized or published. Acceptance here means a self-contained unsigned macOS ZIP, deterministic local regeneration, checksum verification and clean extracted smoke evidence on the supported macOS architecture.
