# Release rules and lessons

This page gives the release rules that come from earlier release failures.
Apply them to each release.
For the procedures, read [macOS release](release-macos.md) and [Windows and Linux release](release-windows-linux.md).

## Find platform failures before the tag

Do focused platform checks on a preparation branch before you select the release commit.
Keep diagnostic runs separate from release candidates.
After a fix, all three release packages must come from the same final tagged commit.
A passing diagnostic branch does not replace the native gates of that commit.

### Windows installer

Run the Windows installer rehearsal before the expensive macOS package and before the tag.
The workflow runs `scripts/release/test_windows_installer_integration.py` on each Windows job.
It covers first install, upgrade, failed startup, registration failure and rollback.

- Give each silent NSIS error dialog an explicit `/SD IDOK`.
- Keep the abort path and the rollback path.
- Leave the staging directory before you rename it, because Windows cannot rename the current directory.
- Treat a startup-failure case as tested only when the new executable really started.

### Production updater configuration

Check the production updater configuration before a long build.
The check must use the real pinned key and the canonical repository and feed URL.
Run `python -m unittest scripts/ci/test_windows_linux_updater_config.py`.
That test runs the real `cmake -P` validation.

- The CMake regex engine does not accept a `{43}` repetition.
- Validate a key with an explicit length check and an allowed-character and padding check.
- Keep the configuration regressions in `scripts/ci/test_windows_linux_updater_config.py`.

### Native compilers

The final commit must pass the GCC and MSVC native gates and the package smoke checks.
A passing macOS suite does not show that a different platform is ready.
Both compilers treat warnings as errors.

- Initialize every field of an aggregate.
- Bind structured range elements by reference.
- Do not use the same variable name again in an `if` and `else if` initializer chain.

### Native thread stacks

Test Rust helpers that native threads call with a small representative stack.
A Windows native thread can have a stack of 1 MiB.
Package hash calculation uses a bounded buffer on the heap.
Keep the 512 KiB stack regression `verifies_package_on_a_small_stack_thread` in `crates/update/tests/metadata.rs`.

## Find the cause of a test failure

If a test fails, find the cause.
Do not add a blind CTest retry.
Do not skip an assertion.
Do not increase a timeout as the only fix.
Use focused stress runs that stop at the first failure.
Inspect the order of the failed signals before you change behavior.

- Buffer diagnostic traces, because synchronous logging can hide a race.
- Capture Windows GUI QtTest output through `scripts/ci/qt_test_output.py` in the CTest command.
- Do not use a second run to show the first failure.
- Give focused native commands the same Qt, QScintilla, path, library, font and `offscreen` configuration as `scripts/ci/desktop.py`.

### Synchronize Qt tests with real readiness

An idle snapshot or one `processEvents()` call does not show that queued work is complete.
Synchronize a test with the real readiness of the work.
Use recovery readiness, the request token, button state or the terminal state of the operation.
Global signal counts can include valid background refreshes.
Filter signals by the request token of the fixture.

### Track shared activity by source

Track shared transient activity by its source object.
One shared busy flag lets an idle tab hide the active read of a different tab.
Then a window can close too early, or navigation can stay blocked.

- Keep the navigation guard while any source is active.
- Remove a source when it is destroyed.
- Defer control refresh from a destruction callback.
  Then the teardown of the parent window cannot access destroyed widgets.
- Keep the regression `overlappingObjectWorkRetainsNavigationGuardUntilEverySourceFinishes` in `tests/desktop/workspace_test.cpp`.

### Other platform lessons

- Compare paths after you normalize the separators.
  Windows does not always return Unix separators.
- For exact-color render assertions, use the real delegate and an explicit device-pixel ratio.
- Do not crop a physical-pixel screenshot with logical coordinates.
- Do not expect an exact pixel color from a small antialiased glyph.
- Keep the geometry, icon, color and accessibility assertions.
- A shared diagnostics service must allow concurrent reads.
- A contended `try_lock` must not lose a concurrent engine result.
- Keep the regression `concurrent_service_read_does_not_drop_an_engine_outcome` in `crates/core/src/actor.rs`.
- For macOS memory tests, sample new pages that do not compress.
  macOS can compress idle pages of repeated bytes.

### macOS full-screen readiness

A window can still have the full-screen mask after `showNormal()`.
Thus the mask does not show that the full-screen transition is complete.
Wait for the real AppKit entry and exit notifications and a safe content layout.
Then request the next state.
Do not use a fixed sleep.

- Use a bounded `QEventLoop::exec()` for the transition.
- Qt on macOS runs the native application run loop in `exec()` mode.
- A manual `processEvents()` loop only flushes events and does not run that loop.
- Keep the layout assertions and the four-second transition limits.
- Remove the observers before the local state and the window are destroyed.

See the [Qt 6.8.3 Cocoa event dispatcher](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/cocoa/qcocoaeventdispatcher.mm).

## Tags and assets

The verified macOS manifest, both candidate receipts and the remote tag must name one exact source commit.

- Replace an unpublished tag only when the user explicitly authorizes it.
- Use an exact `--force-with-lease` against the tag object that you observed.
- Then verify the peeled commit of the new tag.
- Build and verify fresh packages for all three platforms from the new commit.
- Never change a published tag.
- Never change the bytes of a published asset.
- Fix a problem in a published release with a new version.

GitHub's release-by-tag endpoint does not return drafts.
Use the authenticated, paginated release list.
If the creation of a draft is not confirmed, inspect the remote state before you retry.
A new draft can take some time to appear.
Continue only when its tag, notes, draft state and asset bytes match.
Do not make a duplicate draft, and do not overwrite assets.

Publish exactly the DMG, the Windows installer and the AppImage.
Keep local manifests, inventories, the appcast, logs, source archives, license archives and signature configuration out of the Release.
Keep the bundled notices and the local evidence.

## Complete the feed rollout

GitHub Release publication and Pages feed deployment are two separate steps.
The macOS appcast and the signed Windows and Linux JSON feeds are on GitHub Pages.
They refer to the immutable package URLs of the GitHub Release.
Advance all three feeds together, after you verify the public packages.
Until the deployment completes, an installed copy of ChoscorDB can show the previous feed entry as the newest version.

- Announce completion only when the public Release has exactly three verified package assets.
- All three live signed feeds must also show the selected version.
- If the Release and the feeds do not agree, report the gap explicitly.
- Report a partial rollout honestly.
- Keep a published Release separate from feeds that are not complete in the report.
- After a network failure, retry with the same verified inputs.

## Evidence

Record manual clean-machine checks and update checks separately from automated evidence.
A maintainer attestation is not an observed pass or a supplied log.
If a required check is not done, record it as pending.
Do not invent a pass or silently waive a check.
