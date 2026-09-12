# ChoscorDB desktop design

Open `workspace.html` in a browser for the visual reference. Native Qt Widgets must reproduce this layout; HTML is not shipped as the application. The design is established before implementation.

PNG screenshots referenced in these documents are local verification artifacts. They are ignored by Git and excluded from source archives; a fresh checkout does not include them.

## Layout and visual language

Use a QMainWindow with menu bar, left navigator dock (245 px initial width), central editor tabs, query toolbar, vertical editor/results splitter, and status bar. Minimum useful window size is 960 × 640; splitter sizes and dock visibility persist. Default size is 1280 × 900. Use platform fonts, 13 px interface text, 14 px monospace SQL, 8 px spacing increments, restrained borders, and a muted teal accent. Honor system light/dark palette and increased contrast; color never conveys state alone. Native focus indicators, accessible names, keyboard navigation, scalable icons, and screen-reader labels are required.

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

Compare native screenshots at 1280 × 900 against the specimen for hierarchy and spacing. Exercise empty workspace, connection failure, running/cancelling query, NULL versus empty, large-value placeholder, cache eviction, export cancellation, and disconnected recovery. Validate keyboard-only workflows and both palettes. Mockup content is not evidence of implemented behavior.

## Native implementation evidence

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
