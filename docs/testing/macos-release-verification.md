# Local macOS release implementation verification

Date: 2026-09-20. Host: Apple Silicon, macOS 26.5, SDK 26.5.
Implementation working tree based on `1450036e75fbb88aac06cfe99900be96e4a2d5fa`.
The supplied release spec was already untracked; it was preserved unchanged.

## Completed checks

- `python scripts/ci/quality.py full`: passed, including formatting, Ruff,
  actionlint, 41 CI Python tests, 92 release Python tests, Rust format/check/
  Clippy/tests/cargo-deny, native build, and all 36 CTest cases.
- Tests observed missing behavior before implementation for version authority,
  Objective-C++ quality scope, identity, update shutdown/recovery failures,
  publication ordering/retries, archive/inventory consistency, and attribution.
- The production updater configuration compiled and linked against the verified
  Sparkle 2.9.6 archive and pinned Qt 6.8.3. A placeholder public key was used only
  for unsigned build validation; these outputs are not production releases.
- `macos.py prepare` completed using a faster official Qt mirror with trusted
  upstream checksum verification. The isolated cache contains Qt 6.8.3,
  QScintilla 2.14.1 rebuilt with Mach-O minimum 26.0, Sparkle 2.9.6, and verified
  native source archives; 5,476 prepared files were recorded with hashes.
- Installed, relocated, stripped, arm64-only app passed the existing dependency
  verifier and an isolated `--smoke-test`. All 24 Mach-O payloads declare a minimum
  OS no higher than 26.0. This does not demonstrate launch on macOS 26.0.
- Actual staged inventory generation succeeded: 269 files, 122 packages,
  including Qt, QScintilla, Sparkle, SQLite, Lucide, Geist, and shadcn attribution.
- Two real app launches with isolated Application Support fixtures left legacy
  sentinels untouched, created the exact new identity directory without nesting,
  and preserved new-identity database state. `--version` returned `ChoscorDB 0.1.0`.
- The ignored native credential round-trip was run explicitly and passed against
  a fresh UUID under `com.choscor.ChoscorDB.tests.credentials`, including cleanup;
  no real saved credentials were read or changed.
- Python rendered the existing SVG into `AppIcon.icns`; the CMake build installed
  that icon into the application bundle.
- Production preflight rejected this uncommitted checkout as required.

Local logs and unsigned artifacts are under `build/macos-release-evidence/`.
The icon is `AppIcon.icns`; the final prepared-toolchain staged bundle is
`prepared-stage/choscordb.app`, with results in `prepared-verification.json`.
The complete quality log is
`final-quality-reviewed.log`. These ignored outputs are verification aids and
must not be published as release artifacts.

## Independent review

Requirements and code reviews were conducted separately, with authors excluded
from reviewing their own changes. All material implementation findings were
fixed and re-reviewed: omitted canonical metadata, unavailable historical feed
downloads, conflicting version metadata, hardened-runtime detection, source/
checksum/dependency consistency, stale Qt cache acceptance, and asset ownership.
Both axes approved the implementation with the external evidence limits below.

Actual verification also found and corrected existing release-tool assumptions:
deleted shadcn install inputs, stale Lucide hashes, Qt attribution multiline text,
and Qt's short-versus-full framework version fields. Fixture tool successes are
evidence of CLI control flow, not Apple signing or notarization.

## Still required before production publication

- Commit the reviewed release sources and matching version/changelog/lockfile.
  Packaging intentionally cannot produce a release from this working tree.
- Create and securely back up the separate `com.choscor.ChoscorDB` Sparkle key;
  its public-key lookup was unavailable during this run. A Developer ID identity
  and the `agents-notary` profile were available; no personal values were logged.
- Run the complete real signing/notarization/stapling and final DMG verification
  workflow. No signed/notarized production artifact was produced in this session.
- Complete the isolated signed A → B and tampered-update rehearsals, including
  native consent/menu behavior, restart cancellation, and state preservation.
- Install and launch on a clean macOS 26.0 machine/VM. That environment was not
  available here. Other-platform and remote CI execution were not performed.

Nothing was committed, tagged, pushed, or published. See
[the maintainer workflow](../MACOS_RELEASE.md) for the commands and rehearsal.
