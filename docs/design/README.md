# ChoscorDB desktop design

The appearance contract is the pinned [Nova / Neutral reference](shadcn-reference.md).
`workspace.html` illustrates the retained information architecture; it is not
proof of native implementation or gallery approval. The [coverage matrix](shadcn-coverage.md)
tracks application-owned surfaces and the required gallery checkpoint.

## Layout and visual language

Retain the native menu/title bars, left navigator dock (245 logical pixels initially),
editor tabs, stable query actions, editor/results splitter and status bar. Default
and minimum useful window sizes remain 1280×900 and 960×640. Preserve valid geometry,
dock visibility and splitter placement.

Shared appearance follows Base Nova / Neutral with bundled Geist, licensed Lucide
icons, a 4-pixel spacing unit, 32-pixel default controls, explicit 24/28/36-pixel
button variants and reference radii. Only System, Light and Dark are user-facing
appearance choices after the gated migration; legacy density/accent records remain
read-compatible and no longer determine rendering. SQL editor fonts remain
user-controlled. Forced contrast, reduced motion, Unicode fallback and Qt/OS
scaling take priority as documented in the reference exceptions.

Every application-owned surface must use production components and semantic
colors; the preview must use those same definitions. Keyboard access, visible
focus, accessible names and non-color state descriptions remain required.
The working gallery must be reviewed before production layout/screen migration.
Current implementation evidence and outstanding work are recorded in the
[gallery evidence record](shadcn-gallery-evidence.md). The user approved the
gallery on 2026-09-13; [production migration evidence](shadcn-implementation-evidence.md)
records the subsequent work and verification.

## Interaction contract

- Connection dialog: driver selector controls PostgreSQL host/port/database/user/TLS/credential fields or SQLite path/read-only controls. Test runs asynchronously and shows structured errors. Save stores a credential reference. Credential-store failure is explicit; no plaintext fallback. Profile context menu: connect/disconnect, edit, duplicate, test, delete. Deletion confirms; duplicating never copies a password into metadata.
- Navigator: expandable connections → databases → schemas → tables/views → columns/keys/indexes. Show a loading child during fetch and a retryable error on failure. Refresh replaces only the selected subtree. Context menus copy qualified names, show DDL, and generate SELECT/INSERT/UPDATE/DELETE into an editor without executing it.
- Editor: multiple closeable tabs, dirty markers, line numbers, SQL highlighting, bracket matching, find/replace, completion, UTF-8 file open/save, undo/redo. Run executes nonempty selection or statement at cursor. Preferences expose font, shortcuts, page size, cache budgets, timeout, and history retention. Confirm DROP, TRUNCATE, and UPDATE/DELETE without a qualifying WHERE before dispatch.
- Query states: queued, running, cancelling, completed, failed, disconnected appear as text with icons. Cancel stays available while queued/running and becomes disabled while cancelling. Errors are selectable plain text with SQLSTATE/vendor code. Messages show warnings, duration, affected rows. No modal success dialogs.
- Transactions: explicit auto-commit/manual control; manual mode reveals Commit and Rollback. Disconnect/close with an open transaction warns and offers rollback or cancel. Never repeat a write on reconnection.
- Results: QTableView with typed column headers, row numbers, horizontal scrolling, paged navigation, copy cells/rows/page, and loaded-byte budget display. NULL uses italic muted text; empty text remains empty. Large values show a size/type placeholder and open a details dialog on request. Never show a fabricated total or trigger COUNT(*). Pin the visible page and show loading placeholders for missing pages.
- Export: choose CSV, JSON, JSON Lines, SQL INSERT; destination picker, format settings, progress row with Cancel. Keep the destination unchanged until atomic completion. Failure presents a retry action and cleans temporary output.
- History dock: timestamp, profile, SQL excerpt, duration, status, row count; open into editor only. Clear confirms; disable prevents future history writes. Recovery restores disconnected tabs, never executes files or SQL.
- Shutdown: cancel active operations, close workers with a bounded deadline, preserve editor state; never block the UI event loop waiting on database I/O.

## Design acceptance

Compare individual controls in the 1280 × 900 native gallery captures against the pinned browser reference at their logical dimensions. The [coverage matrix](shadcn-coverage.md) links each comparison. Exercise empty workspace, connection failure, running/cancelling query, NULL versus empty, large-value placeholder, cache eviction, export cancellation, and disconnected recovery. Validate keyboard-only workflows and both palettes. Mockup content is not evidence of implemented behavior.

## Earlier native implementation evidence

The images below document existing application behavior before the shadcn migration.
They are not acceptance evidence for the new visual system.

`native-workspace.png` is captured by the native MainWindow smoke test while executing a real `SELECT 3 AS result` against SQLite through CXX. It shows the system light palette and design spacing. `native-foundation.png` preserves the earlier empty-workspace render. Screenshots prove rendering only; behavior is covered by the Qt and Rust tests documented in `../BUILD.md`.

## Large-value detail interaction

Activate a large-value placeholder with double-click or the platform table activation key. Open one nonmodal detail window per workspace. Show a byte-range status above a two-column table (byte offset and text or hexadecimal), with Previous, Next, and Close below. Text displays bounded segments preserving UTF-8 character boundaries; binary displays sixteen bytes per row. Navigation replaces the current window of data and shows a loading status. Query replacement or disconnect closes the detail window. Errors stay inside the detail window and leave the result grid usable. The native implementation requests at most 64 KiB per window; it never builds a document containing the complete value.

`native-value-detail.png` is captured by the native SQLite detail-view test on the second binary window. Text, UTF-8 boundaries, and disconnect behavior are checked separately by native and model tests.

The export layout and state transitions are specified in [export.md](export.md).

Saved-profile layout and behavior are specified in [profiles.md](profiles.md).

History and disconnected workspace restoration follow [history-recovery.md](history-recovery.md).

`native-recovery.png` shows a restored dirty Unicode buffer with no active connection; the native restart tests verify disconnected behavior and subsequent close-time persistence.

`native-history.png` records the populated history dock using a real two-row SQLite query. The native flow tests additionally verify reopening without execution, disabling/clearing history, saved-profile association, and close-time persistence.

Editor search and replacement follow [editor-search.md](editor-search.md). `native-search.png` shows the native panel after replacing two matches without a database connection.
