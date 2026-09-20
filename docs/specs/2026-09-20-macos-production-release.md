# macOS production releases and automatic updates

Status: Approved for implementation
Date: 2026-09-20
Source: Brainstorm with the repository owner; reference application in the maintainer's separate local Agents checkout.

## Outcome and conversation decisions

Give the maintainer a repeatable local workflow to build, sign, notarize, verify, and separately publish ChoscorDB. Users install a DMG and receive authenticated updates without losing database work or saved application state.

The owner chose platform-specific updaters, with Sparkle on macOS first. Windows and Linux are planned but their updater and packaging implementations are deferred. The owner accepted the recommendations below, explicitly selected a clean identity change without migration, and finally set the minimum supported macOS version to **26.0**, superseding earlier 14.0 and 26.5 proposals.

## Current workspace findings

- ChoscorDB is a Qt 6 Widgets/C++ application with a Qt-independent Rust engine, built through CMake and Corrosion. `CMakeLists.txt` and `Cargo.toml` currently identify version 0.1.0.
- `CMakePresets.json` has a release preset with `BUILD_TESTING=OFF`. Installation already rejects developer-preview builds.
- `cmake/Deploy.cmake` and `scripts/release/stage.py` provide optional unsigned staging, deployment of Qt/QScintilla, dependency validation, payload hashes, relocation, and an isolated smoke test.
- Existing release tooling includes `source_archive.py`, `prepare_qt_notices.py`, `cargo_licenses.py`, and `notices_sbom.py` under `scripts/release/`. Extend these capabilities rather than bypassing their verification.
- Dependency pins and bootstrap behavior are documented in `docs/CI.md`, `scripts/ci/desktop.py`, `scripts/ci/bootstrap_qscintilla.py`, `scripts/ci/requirements.txt`, and `rust-toolchain.toml`. The current native dependency baseline is Qt 6.8.3 and QScintilla 2.14.1. Reconcile actual SDK/dependency compatibility during implementation; do not silently substitute Homebrew packages or raise the agreed minimum OS.
- The current bundle ID in `CMakeLists.txt` and credential service in `crates/credentials/src/lib.rs` are `org.choscordb.desktop`. `desktop/app/main.cpp` derives storage through Qt application/organization names and `QStandardPaths`.
- `MainWindow::closeEvent` in `desktop/app/main_window.cpp` resolves pending table edits, flushes preferences/recovery, requests shutdown confirmation, and waits for database shutdown. `QueryWorkspace::confirmShutdown` handles active work and transactions.
- The reference app's `scripts/package.sh` and built `dist/Agents.app/Contents/Info.plist` both use `com.choscor.Agents`. Its package/upload scripts demonstrate Developer ID signing, notarization, Sparkle, and R2 publishing. They are examples, not code to copy unchanged: their automatic tagging, version detection, and shared Sparkle-key assumptions differ from this spec.
- The inspected local machine is arm64 on macOS 26.5. This is not evidence of successful execution on macOS 26.0.

## Scope and identity

1. Ship Apple Silicon only, with minimum **macOS 26.0**, outside the Mac App Store.
2. Set the bundle ID and production credential-store service to **`com.choscor.ChoscorDB`**, preserving exact case. Rename related test namespaces and identity documentation consistently.
3. Store production application data beneath a directory named `com.choscor.ChoscorDB` in the user's macOS Application Support location. Preserve the database filename `choscordb.sqlite`. Ensure all app-owned profiles, history, preferences, and recovery use the new identity consistently; do not accidentally create a second nested identity path through Qt defaults.
4. Keep the display name **ChoscorDB**, executable `choscordb`, and established `CHOSCORDB_*` configuration prefix. Identity renaming does not rename Rust crates or the project vocabulary.
5. Start fresh: do not read, migrate, overwrite, or delete old application data or credential entries. No legacy fallback. This behavior must be documented clearly for existing development users.
6. Normal development builds must remain usable without signing credentials or release configuration. Keep platform-specific updater integration outside the Rust database engine and behind a small desktop boundary suitable for future native backends.

Non-goals: Intel/universal distribution; Windows/Linux packaging or updater implementation; a cross-platform updater framework; beta channels; App Store distribution; CI-hosted signing/publishing; automatic Git tags or pushes; legacy-data migration; automatic downgrades; unrelated product/UI redesign.

## Local maintainer workflow

### Prepare

Provide documented repository-owned commands for dependency preparation, production packaging, verification, and publishing. Thin shell entry points may delegate to the existing Python/CMake tooling. Exact internal module layout is an implementation choice.

- Prepare pinned Qt, QScintilla, and Sparkle dependencies in an isolated local directory, with verified downloads and reusable caches. Pin Sparkle's framework and associated tools compatibly.
- Apply the macOS 26.0 target consistently to app code and locally built native/Rust dependencies. Verify bundled Mach-O architectures and minimum-OS requirements rather than trusting only the app plist.
- Reuse the reference app's Apple Developer signing identity and `agents-notary` Keychain profile, with explicit configurable overrides. Discover identity locally; do not copy personal identity details or credentials into documentation or logs.
- Set up a separate ChoscorDB Sparkle signing key in Keychain using an explicit app-specific key account. Never silently reuse the reference app's default key. Embed only the public key in the app. Document setup, backup, and recovery considerations without exporting secrets into the repository.
- Diagnose missing tools, incompatible dependencies, unavailable signing identity/profile/key, and invalid release inputs before expensive production work where practical.

### Package and verify

- Production packaging requires a clean tracked/untracked source checkout, excluding normal ignored build outputs, a checked-in stable `X.Y.Z` release version, and a matching `CHANGELOG.md` entry.
- Establish one authoritative checked-in release version and validate/synchronize dependent app metadata through the build. Do not edit tracked version files as a side effect of packaging. Validate any command-line version argument against that authority.
- Use consistent versions in the bundle, displayed application version, filenames, manifest, and appcast. Require increasing stable release versions; avoid commit-count versioning that can disagree across branches.
- Build a release without tests, galleries, or developer-only payloads; stage a self-contained relocatable `.app` with its required frameworks/plugins/resources. Reuse the existing icon artwork for normal app/DMG identification; no new branding design is required.
- Generate notices and software inventory covering the actual payload, including Sparkle and native dependencies. Produce matching source material using the existing provenance tooling, updating exclusions as needed to include all build-required assets while excluding secrets and local data.
- Sign nested code and the app with Developer ID/hardened runtime as appropriate, notarize and staple, create a drag-to-install DMG with an Applications shortcut, then sign/notarize/staple the DMG. Verify final artifacts after all mutations.
- Produce `ChoscorDB-X.Y.Z.dmg`, `ChoscorDB.dmg`, `choscordb-appcast.xml`, matching source archive(s), notices, checksums, software inventory, and a release manifest recording version, source commit, dependencies, artifact hashes, and verification results.
- Keep outputs in ignored release directories. Packaging does not upload, create Git tags, push, or modify committed feed history. If development/skip-notarization modes exist, clearly distinguish their outputs and make them ineligible for production publication.
- Preserve useful sanitized logs and notarization diagnostics on failure. Never report a failed or incomplete package as release-ready.

### Publish

- Provide a separate explicit publish command and a dry-run that displays intended destinations/actions without changing local feed history or remote objects.
- Use Cloudflare R2 bucket `choscor-downloads` and base URL `https://cdn.choscor.com`, configurable through documented settings. Authenticate through existing local tooling; never embed credentials.
- Publish the agreed names `ChoscorDB-X.Y.Z.dmg`, `ChoscorDB.dmg`, and `choscordb-appcast.xml`, plus version-associated source/metadata artifacts. Do not touch Agents artifacts.
- Select a release through an explicit version or unambiguous verified manifest; never guess from lexicographic or modification-time ordering of DMGs.
- Refuse invalid signatures, missing notarization, incomplete verification, changed artifacts, mismatched versions, or incompatible feed configuration.
- Treat versioned artifacts as immutable: reject different bytes at an already published version; permit retries with identical bytes. Protect against an older/stale publication replacing a newer stable feed or latest alias.
- Preserve previous appcast entries, validate existing remote state, upload immutable payload/source/metadata first, then update the latest download and finally the feed. Verify public download availability before exposing the new feed. Do not infer remote absence from authentication/network errors.
- Partial failures remain diagnosable and retryable. The feed must never advertise an unavailable artifact. Document the single-maintainer publication model and prevent overlapping local publishes.
- Fix a faulty public release by shipping a higher version. No automated app/data downgrade or deletion of prior releases.
- Local explicit Git tagging/pushing remains a documented maintainer step, separate from packaging and publishing.

## User update behavior

- One stable channel, using the production appcast above and ChoscorDB's public update key.
- Use Sparkle's standard native update UI, with a **Check for Updates…** item in the macOS app menu and an accessible persisted preference for automatic checks. Ask consent for automatic checks; installation/restart always requires user approval. No silent installation mode in this phase.
- Suppress production update checks in development builds, smoke/screenshot runs, and isolated tests. Unsupported platforms must build without Sparkle dependencies or a misleading enabled update action.
- A manual check reports up-to-date, available-update, or failure states. Background network/feed failures must not disrupt normal database work. Reject invalid signatures and incompatible updates without replacing the installed app.
- Installation/relaunch must pass through normal app shutdown: resolve pending grid edits, preserve SQL recovery/preferences, confirm cancellation/rollback of active work, and wait for shutdown completion. Never bypass this with a direct forced process exit.
- If the user cancels or recovery/persistence fails, postpone installation and leave the working app usable. The user can retry later. Do not silently discard edits or commit/roll back transactions merely because an update was downloaded.
- Updating two releases using the new identity preserves profiles, history, preferences, recovery, and credential references. The clean-start policy applies only to the old identity, not subsequent updates.

## Acceptance criteria and public test seams

| Observable outcome | Test seam / evidence |
|---|---|
| Documented commands prepare dependencies and produce a traceable release; bad inputs fail clearly | Release CLI exit status, diagnostics, and manifests; temporary fixture repositories and injected external-tool executables for meaningful failure cases |
| Packaging performs no publication or automatic Git operations | CLI integration checks observing repository state and external command invocations |
| Bundle runs after relocation without developer-machine library paths | Existing stage verifier, `otool`/Mach-O inspection, installed bundle `--smoke-test`, and interactive launch in a clean user environment |
| arm64 and macOS 26.0 constraints are consistent | Bundle plist and every bundled executable/library's architecture/minimum-OS inspection; actual macOS 26.0 installation/launch check before first public release |
| Correct identity and clean start leave legacy data untouched | Installed bundle metadata; launch with fixture old/new Application Support directories; native credential API tests using isolated renamed test namespaces; never test against real saved secrets |
| Final app/DMG are validly signed, notarized, and stapled | Real `codesign`, `notarytool`, `stapler`, and Gatekeeper assessments against final artifacts, with recorded results |
| Version, changelog, source, signatures, checksums, and inventory agree | CLI validation of the final release manifest and artifacts, including altered-artifact rejection |
| Publishing is safe to retry and feed-last | Publish CLI against a controlled object-store/tool fixture: dry-run, partial upload, identical retry, conflicting version, stale feed, unavailable public download, and overlapping invocation cases |
| New update is offered; no update and failures behave appropriately | App menu/settings through the desktop updater boundary and controlled feeds; real Sparkle exercised by the signed rehearsal |
| Restart cancellation/persistence failures preserve the running workspace | App-level shutdown/update tests through user actions, real recovery and query-workspace seams, isolated SQLite data and owned database fixtures where needed |
| Signed A → B replacement succeeds and preserves new-identity data | Two real signed/notarized test releases with a controlled nonproduction feed, dedicated test credentials/key/data, and observed install/relaunch/version/state |
| Tampered update cannot replace the app | Controlled signed-rehearsal feed serving altered content; unchanged installed app and visible failure evidence |
| Normal builds/tests do not contact production update endpoints | Development/smoke execution with observed network/update adapter calls; relevant existing native/non-macOS build checks |

Use the highest practical existing boundaries for tests. Do not substitute mocked command success for real signing, notarization, or Sparkle integration evidence. Run applicable repository quality checks from `docs/CI.md`; distinguish existing failures from regressions. CI remains an unsigned verification path.

## Rollout, limitations, and deferred work

- This implementation delivers scripts, app integration, documentation, and locally verified production-capable artifacts. Do not publish to the production feed or push tags merely to demonstrate completion. A maintainer explicitly invokes publishing as a separate release action.
- Perform the update rehearsal in an isolated environment with distinct test identity/storage/key/feed configuration so it cannot modify production installations or user databases. Release validation must reject rehearsal configuration as production output.
- Local verification can run on the inspected macOS 26.5 machine. A clean macOS **26.0** machine/VM installation and launch check is a required manual prerequisite before the first public release. Record it as pending if unavailable; do not claim minimum-OS verification from a deployment target alone.
- Preserve signing-key continuity across releases. Provide operational notes for certificate/key renewal, failed notarization, interrupted publication, and shipping a higher-version fix; automated key rotation is deferred.
- Future Windows and Linux work may share version/changelog/manifest conventions and hosting but must choose and validate its own packaging/updater backends. Linux package-managed installations may require different update ownership. None is implemented here.
- Actual SDK availability, pinned dependency compatibility with macOS 26, native framework inventory, and Qt/Sparkle termination interoperation require inspection during implementation. Fail explicitly rather than weaken signing, raise the minimum OS, or bypass shutdown protections.
- The existing notices/source tooling is a starting point, not proof that all release payload provenance is complete. Verify coverage of the final contents.

## Relevant references

- Local build and release context: `docs/BUILD.md`, `docs/CI.md`, `CMakeLists.txt`, `cmake/Deploy.cmake`, `scripts/release/`, `scripts/ci/`.
- Reference workflow in that separate Agents checkout: `README.md`, `scripts/package.sh`, and `scripts/upload.sh`. No files or credentials from that checkout are part of this repository.
- [Sparkle Qt/programmatic integration](https://sparkle-project.org/documentation/programmatic-setup/)
- [Sparkle distribution and key setup](https://sparkle-project.org/documentation/)
- [Sparkle publishing updates](https://sparkle-project.org/documentation/publishing/)
- [Sparkle update/relaunch delegate](https://sparkle-project.org/documentation/api-reference/Protocols/SPUUpdaterDelegate.html)

## Fresh-session handoff

Read this entire spec, inspect the current workspace and applicable instructions, and invoke `$implement` with `docs/specs/2026-09-20-macos-production-release.md`. Treat this document as the approved requirements. Reinspect code that may have changed since the brainstorm, preserve unrelated user work, and report real verification evidence plus the explicitly deferred macOS 26.0 manual check. Do not treat implementation as authorization to publish a public release.
