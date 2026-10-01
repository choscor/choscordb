# Release verification lessons

Recorded after the v0.1.8 release on 2026-09-30. Release source:
`ecb7ddfc53bec6c56b6563a94f5588a2a7e1f2e1`. The final
[Windows/Linux workflow](https://github.com/choscor/choscordb/actions/runs/36652130433)
passed both native suites, production builds, package smoke checks, and artifact upload.

## Catch platform failures before tagging and notarizing

Run focused platform checks on a preparation branch before selecting the clean
release commit. Keep diagnostic runs separate from release candidates. After a
fix, all three release packages must still come from the same final tagged commit;
a passing diagnostic branch cannot substitute for its native release gate.

- Run the real Windows installer integration rehearsal early, including first
  install, upgrade, failed startup, registration failure, and rollback. Silent
  NSIS error dialogs need an explicit `/SD IDOK`; preserve abort and rollback.
  Leave the staging directory before renaming it: Windows cannot rename its
  current directory. Require evidence that the new executable actually launched
  before treating a startup-failure case as exercised.
- Validate the production updater configuration, including the real pinned key
  and canonical repository/feed URL, before a long build. CMake's regex engine
  does not accept the `{43}` repetition used in the original key validator.
  Use explicit length plus allowed-character/padding checks. Keep the actual
  `cmake -P` configuration regressions in
  `scripts/ci/test_windows_linux_updater_config.py`.
- Build with GCC and MSVC warnings treated as errors. Initialize every aggregate
  field, bind structured range elements by reference, and avoid reusing variable
  names across an `if`/`else if` initializer chain.
- Exercise Rust helpers called from native threads with a representative small
  stack. Package hashing uses a bounded heap buffer; a 1 MiB stack buffer overflowed
  a Windows native test thread. Retain the 512 KiB-stack regression in
  `crates/update/src/lib.rs`.

## Diagnose timing failures without weakening assertions

An idle snapshot or one `processEvents()` call does not establish completion of
queued work. Synchronize with recovery readiness, the relevant request token,
button enablement, or the operation's terminal state. Global signal counts can
include legitimate background refreshes; filter by the fixture's request token.

For intermittent failures, use focused stress runs that stop at the first
failure. Buffer diagnostic traces so synchronous logging does not hide a race.
Inspect the failed signal ordering before changing behavior. Do not add blind
CTest retries, skip an assertion, or increase a timeout as the only fix.

- The recent-object failure was caused by object-data tabs overwriting one shared
  busy boolean. An idle tab could hide another tab's active read, allow premature
  closure, and leave navigation blocked. Track activity by its QObject source,
  retain the guard while any source is active, and remove destroyed sources.
  Defer control refresh from destruction callbacks so parent-window teardown
  cannot access already-destroyed widgets. Keep the overlapping-source and
  destruction regression in `tests/desktop/workspace_test.cpp`.
- Compare paths after separator normalization. Test the same path identity rather
  than assuming Windows returns Unix separators.
- Use the real delegate and an explicit image device-pixel ratio for exact-color
  rendering assertions. Do not crop a physical-pixel screenshot with logical
  coordinates or depend on a small antialiased glyph retaining an exact pixel.
  Preserve geometry, icon, color, and accessibility assertions.
- Capture Windows GUI QtTest output through `scripts/ci/qt_test_output.py` in the
  canonical CTest invocation. Do not depend on a second run to reveal the first
  failure. Focused native commands need the same Qt/QScintilla PATH, library,
  plugin, font, and offscreen configuration as `scripts/ci/desktop.py`.
- A shared diagnostics service must permit concurrent reads; a contended
  `try_lock` must not silently lose a concurrent engine outcome. Preserve the
  nonblocking shared-read regression in `crates/core/src/actor.rs`.
- For macOS RSS tests, sample freshly touched noncompressible pages. Idle repeated
  bytes may be compressed by the OS; waiting before allocation avoids measuring
  compression instead of the intended memory-growth threshold.

## Preserve release identity and finish the feed rollout

- A replaced unpublished tag requires explicit user authorization. Use an exact
  `--force-with-lease` against the observed tag object, then verify its peeled
  commit. Rebuild and verify all three candidates for the new source. Once public,
  keep the tag and package bytes immutable and fix shipped problems in a new version.
- GitHub's by-tag endpoint excludes drafts. Use the authenticated paginated release
  list and inspect remote state after a creation-confirmation failure. A freshly
  created draft may not appear immediately. Resume only after its tag, notes,
  draft status, and existing asset bytes match; do not create duplicates or clobber.
- Publish exactly the DMG, Windows installer, and AppImage. Keep local manifests,
  inventories, appcasts, logs, source/license archives, and signing configuration
  out of Release attachments. Preserve bundled notices and retained local evidence.
- The macOS appcast and signed Windows/Linux JSON feeds live on GitHub Pages;
  they reference immutable GitHub Release package URLs. Advance all three feeds
  together after verifying the public packages. The initial bootstrap contained
  only the v0.1.7 macOS appcast, so Windows/Linux feed URLs returned 404 until the
  first deployment. During the rollout, an installed v0.1.8 app could misleadingly
  describe the old v0.1.7 feed entry as the newest available version.
- Announce completion only when the public Release has exactly three verified
  assets and all three live feeds advertise the selected version. Report any
  Release/Pages gap explicitly. Retain the same verified inputs when recovering
  from a network failure; distinguish a published release from unfinished feeds.
- Record manual clean-machine/update checks separately from automated evidence.
  A maintainer attestation is not an independently observed pass or a supplied log.


## Native macOS full-screen readiness

The v0.1.9 preparation run on macOS 15.7 reported a title-bar failure while the
native window still had its full-screen mask after `showNormal()`. The test had
accepted that mask as entry readiness and slept for a fixed 1.2 seconds.
Wait for actual AppKit entry/exit notifications and safe content layout before
requesting the next state. Use a bounded `QEventLoop::exec()` for the transition:
Qt's Cocoa dispatcher uses the native application run loop in exec mode, whereas
manual `processEvents()` pumping only flushes events. A local diagnostic waiting
for the notification with manual pumping failed; the bounded native loop passed.
Retain the layout assertions and four-second transition limits. Remove observers
before the local state and window are destroyed.

Reference: [Qt 6.8.3 Cocoa event dispatcher](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/cocoa/qcocoaeventdispatcher.mm).
