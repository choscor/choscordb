# SQL document targets and shared result ownership

This is the first post-component slice of the
[MVP flow specification](../specs/2026-09-13-mvp-ui-flows.md), covering D1 and the
document-target portions of D3 (AC3, AC6 and AC7). The user's request to implement
the next step with components already done authorizes proceeding beyond the
component checkpoint. Central screens, object browsing and modal conversions
remain subsequent slices; this is not completed-screen acceptance.

## Baseline

- Starting revision: `3c793bf555f02ef0392c9e03fbe21104d38e1552`.
- Starting tracked and untracked Git status: clean.
- Baseline workspace tests: 30 passed, no failures, one PostgreSQL fixture test
  skipped. Command: `QT_QPA_PLATFORM=offscreen
  build/ci/native/choscordb-workspace-tests`.
- The earlier component report links `mvp-reference/manifest.json` and
  `mvp-native/README.md`, but those artifacts are absent from this checkout.
  Their historical capture/review claims cannot be reverified here. Behavioral
  evidence for this slice does not establish full prototype visual fidelity.

## Contract and boundaries

The existing query connection selector is the explicit target of the active SQL
document. Sidebar browsing is independent. Each document retains its runtime
target when another document becomes active; connecting or disconnecting another
session must not silently retarget it. Persisted profile associations remain
compatible, and runtime session identifiers are not restored across restarts.

One shared SQL result and message lifecycle remains. Its origin is captured at
submission, so later document selection or renaming cannot rewrite its source.
Starting another execution replaces the old lifecycle. Execution navigation
guards keep the executing document and Cancel reachable until active work
settles, without treating an idle retained paging cursor as a running query.

Tests use actual MainWindow controls and real SQLite connections with distinct
database paths to detect wrong-target execution. Adapter events may control
timing and failure cases; they do not replace the real execution path.

## Implementation and red → green evidence

- `documentsKeepTargetsAndImmutableResultOrigin` first failed because the real
  SQLite result summary omitted its document title. It now observes different
  `pragma_database_list` paths for two documents, preserved target selection,
  immutable result origin, no execution from switching, and retained text,
  selection, scroll position and functional undo.
- `activeExecutionKeepsDocumentAndCancelReachable` first failed because a pointer
  click selected another tab during a real recursive SQLite aggregate. The test
  now covers pointer, Ctrl+Tab, tab overflow, New query and programmatic switching;
  a visible Cancel button inside the 960×640 window accepts a pointer click and
  immediately shows disabled “Cancelling…” until the query settles.
- Removing the disconnect signal blocker reproduced silent fallback to the
  remaining connection. The restored implementation leaves the draft visibly
  disconnected with Run disabled; explicitly choosing the other target restores
  Run. Mutation red: `/tmp/choscordb-document-disconnect-red.log`.
- Removing runtime target clearing during document restoration caused the
  history reopen test to inherit an active connection. Restoring the guard keeps
  history disconnected and inert. Mutation red:
  `/tmp/choscordb-document-history-red.log`.

The connection selector retains its existing object name and gains the accessible
name “SQL document connection target.” Document target changes refresh completion
and disconnected-state controls. Numbered new-document titles distinguish drafts.
Result status uses plain text with wrapping; the Messages pane starts each new
execution with the same captured origin. No bridge, driver, storage schema, result
cache or export ownership contract changed.

## Focused and native verification

Offscreen: navigator SQL workspace 6 passed; workspace 30 passed and one
PostgreSQL fixture test skipped; modern UI 15 passed. Counts include Qt setup and
cleanup. Formatting and `git diff --check` passed.

Native input on macOS 26.5, arm64, Qt 6.8.3, using the Cocoa platform:

```sh
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-navigator-sql-workspace-tests
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-workspace-tests mainWindowRunsRealQuery mainWindowHistoryRecordsOpensDisablesAndFlushes
```

Both commands passed: 6 and 4 Qt outcomes respectively. Logs are
`/tmp/choscordb-document-cocoa.log` and
`/tmp/choscordb-document-cocoa-workspace.log`. The real-query widget capture is
`build/ci/document-ownership-evidence/workspace-cocoa.png` (1280×900 logical
content, Light, system UI/default editor font, temporary in-memory SQLite fixture).
It visibly shows the captured document/connection origin and the real result `3`.
The history fixture capture is alongside it. These local build artifacts are not
pixel-fidelity evidence or a replacement for the later screen comparison matrix.

The restart test also passed on Cocoa (3 Qt outcomes):

```sh
QT_QPA_PLATFORM=cocoa build/ci/native/choscordb-query-settings-workspace-tests defaultsApplyOnlyToFutureQueriesAndSurviveRestart
```

## Independent review

- Requirements: approved for this slice. The minor request to assert visible
  pending “Cancelling…” text was implemented and re-reviewed; no open findings.
- Code: approved, no findings. The reviewer inspected target initialization,
  recovery, signal blocking, navigation, generated/history SQL and existing
  query/paging/export/disconnect ownership.

The first full run identified four existing tests across query-settings,
disconnect and completion suites that assumed new connections automatically
retargeted existing/restored documents. Their setup now selects targets explicitly;
original settings, rollback and completion outcomes remain asserted. Both reviewers
approved these updates, and all three focused suites passed (8.13 seconds).

## Final integrated verification

The canonical command passed after the reviewed test updates:

```sh
PATH="$PWD/build/ci/python/bin:$PWD/build/ci/tools/bin:$PATH" build/ci/python/bin/python scripts/ci/quality.py full
```

Final log: `/tmp/choscordb-document-quality-final.log`. C++ formatting, Ruff
lint/format, actionlint, 74 Python tests, Rust format/check/clippy, 261 Rust tests
(23 explicitly ignored), cargo-deny and the Release native build passed. All 33
CTest suites passed in 37.74 seconds. The PostgreSQL fixture-dependent native
case remained skipped; remote cross-platform and live PostgreSQL acceptance were
not run. Final `git diff --check` passed.

The next slice is central Start/SQL/Object/History navigation with shared
operation guards. Typed object metadata, separate bounded object Data, profile
flow migration, modal Export/Preferences, upgrade integration and completed-screen
reference comparisons remain subsequent work under the original spec.

## Changed files

Production: `desktop/app/{main_window,query_workspace}.{h,cpp}` and
`desktop/widgets/sql_editor/sql_editor.{h,cpp}`.
Tests: `tests/desktop/{navigator_sql_workspace_test,workspace_test}.cpp`.
Existing restart, disconnect and completion fixtures in
`tests/desktop/{query_settings_workspace_test,disconnect_workspace_test,editor_completion_test}.cpp`
now choose a different or reconnected target explicitly. Their settings,
transaction rollback and metadata assertions remain; restart additionally
observes zero executions before the explicit Run action.
Documentation: this report, `docs/design/README.md`, and the historical
`docs/design/mvp-implementation-evidence.md` status pointer. The approved spec is
unchanged. No commit, push, pull request or publication was made.
