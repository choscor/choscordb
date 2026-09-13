# ChoscorDB desktop design

The active visual/layout authority is [`../mvp-design/`](../mvp-design/README.md),
with production behavior governed by the approved
[MVP UI flows specification](../specs/2026-09-13-mvp-ui-flows.md).
The Nova/Neutral captures and approvals below are historical; they do not approve
the replacement components or screens.

## Component organization

See [Qt and Telegram architecture research](qt-component-architecture.md) for the
customization strategy and upstream source references. The implemented build
boundaries are declared in [`DesktopComponents.cmake`](../../cmake/DesktopComponents.cmake):

| Target | Owns | Depends on |
| --- | --- | --- |
| `choscordb-design-system` | Foundations, control styles/painting, reusable controls, shared dialogs and notices | Qt Widgets/Svg and platform integration |
| `choscordb-desktop-services` | Bridge adapters, models and appearance preferences | Design system, Qt Concurrent, Rust bridge |
| `choscordb-widgets` | Editor, search, history and feature dialogs | Design system, services, QScintilla |
| `choscordb-desktop` | Main window, workspace and controller composition | Widgets; preview in development builds |
| `choscordb-preview` | Developer specimens and captures under `desktop/tools/preview/` | Widgets; only built with `BUILD_TESTING=ON` |

The [complete component catalog](../../desktop/design_system/README.md) maps every
existing control family to its owning styles, painting and event code. This
includes stock Qt controls, shared dialog presentation and notifications as well
as Button, ButtonGroup and Text. Colors, metrics, fonts and token discovery have
separate foundation modules. `ControlStyle` coordinates the extracted helpers;
`style/` assembles fragments in their original cascade order.

Feature widgets retain matching header/source directories under
`desktop/widgets/<component>/`. DialogShell, ConfirmationDialog and ToastRegion
now belong to the design system; consumers include their owning headers directly. Component APIs, existing Qt classes and dynamic-property contracts stay
unchanged. Add sources through the quality helpers in the appropriate target.
The source policy rejects dependencies from the design system to higher layers.

The existing gallery contains feature specimens, so it links the widget layer.
Pure design and shared-modal tests link only the design-system library and
exercise its resource registration without Rust or QScintilla.

Verify with `cmake --build --preset dev`, `ctest --preset dev`, and
`cmake --build build/dev --target choscordb-header-check`. To build just the
reusable controls, use `cmake --build build/dev --target choscordb-design-system`.

## Delivery and evidence

Implementation is at the component foundation stage. See
[MVP implementation evidence](mvp-implementation-evidence.md) for the baseline,
reference manifest, coverage, verification and outstanding review checkpoints.
Production screen migration follows the user's review of the concrete gallery.

The replacement uses green semantic colors, platform UI typography and compact
panes, with System/Light/Dark and preserved custom SQL editor fonts. Native title
bars, OS menus and system file pickers remain native. Accessibility, visible
keyboard focus, Unicode, reduced motion and Qt/OS scaling take precedence over
literal matching where an actual exception is documented.

Start, SQL workspace, Object explorer and History become central screens with a
full-height sidebar. SQL tabs retain one explicitly attributed shared SQL result;
read-only object data owns separate bounded state. Selecting a sidebar connection
never silently retargets an existing document. Export and Preferences become
modal; large-value detail remains nonmodal. These are target behaviors, not claims
that the current screen migration is complete.

Legacy accent/density settings remain readable without rendering effects. Saved
profiles, credential references, SQL/recovery buffers, history, shortcuts, editor
fonts and compatible geometry/splitters must survive. Only incompatible dock
placement may be reset.

## Interaction contract

- Connection dialog: driver selector controls PostgreSQL host/port/database/user/TLS/credential fields or SQLite path/read-only controls. Test runs asynchronously and shows structured errors. Save stores a credential reference. Credential-store failure is explicit; no plaintext fallback. Profile context menu: connect/disconnect, edit, duplicate, test, delete. Deletion confirms; duplicating never copies a password into metadata.
- Navigator: expandable connections → databases → schemas → tables/views → columns/keys/indexes. Show a loading child during fetch and a retryable error on failure. Refresh replaces only the selected subtree. Context menus copy qualified names, show DDL, and generate SELECT/INSERT/UPDATE/DELETE into an editor without executing it.
- Editor: multiple closeable tabs, dirty markers, line numbers, SQL highlighting, bracket matching, find/replace, completion, UTF-8 file open/save, undo/redo. Run executes nonempty selection or statement at cursor. Preferences expose font, shortcuts, page size, cache budgets, timeout, and history retention. Confirm DROP, TRUNCATE, and UPDATE/DELETE without a qualifying WHERE before dispatch.
- Query states: queued, running, cancelling, completed, failed, disconnected appear as text with icons. Cancel stays available while queued/running and becomes disabled while cancelling. Errors are selectable plain text with SQLSTATE/vendor code. Messages show warnings, duration, affected rows. No modal success dialogs.
- Transactions: explicit auto-commit/manual control; manual mode reveals Commit and Rollback. Disconnect/close with an open transaction warns and offers rollback or cancel. Never repeat a write on reconnection.
- Results: QTableView with typed column headers, row numbers, horizontal scrolling, paged navigation, copy cells/rows/page, and loaded-byte budget display. NULL uses italic muted text; empty text remains empty. Large values show a size/type placeholder and open a details dialog on request. Never show a fabricated total or trigger COUNT(*). Pin the visible page and show loading placeholders for missing pages.
- Export: choose CSV, JSON, JSON Lines, SQL INSERT; destination picker, format settings, progress row with Cancel. Keep the destination unchanged until atomic completion. Failure presents a retry action and cleans temporary output.
- History screen: timestamp, profile, SQL excerpt, duration, status, row count; open into editor only. Clear confirms; disable prevents future history writes. Recovery restores disconnected tabs, never executes files or SQL.
- Shutdown: cancel active operations, close workers with a bounded deadline, preserve editor state; never block the UI event loop waiting on database I/O.

## Design acceptance

Compare individual controls in the 1280 × 900 native gallery captures against the fully rendered MVP browser reference at their logical dimensions in Light/Dark, at 1280×900 and 960×640. The [coverage matrix](mvp-coverage.md) maps specimens and input evidence; [native comparisons](mvp-native/README.md) record capture boundaries and exceptions. Exercise empty workspace, connection failure, running/cancelling query, NULL versus empty, large-value placeholder, cache eviction, export cancellation, and disconnected recovery. Validate keyboard-only workflows and both palettes. Mockup content is not evidence of implemented behavior.

## Earlier native implementation evidence

The images below document existing application behavior before the shadcn migration.
They are not acceptance evidence for the new visual system.

`native-workspace.png` is captured by the native MainWindow smoke test while executing a real `SELECT 3 AS result` against SQLite through CXX. It shows the system light palette and design spacing. `native-foundation.png` preserves the earlier empty-workspace render. Screenshots prove rendering only; behavior is covered by the Qt and Rust tests documented in `../BUILD.md`.

## Large-value detail interaction

Activate a large-value placeholder with double-click or the platform table activation key. Open one nonmodal detail window per workspace. Show a byte-range status above a two-column table (byte offset and text or hexadecimal), with Previous, Next, and Close below. Text displays bounded segments preserving UTF-8 character boundaries; binary displays sixteen bytes per row. Navigation replaces the current window of data and shows a loading status. Query replacement or disconnect closes the detail window. Errors stay inside the detail window and leave the result grid usable. The native implementation requests at most 64 KiB per window; it never builds a document containing the complete value.

`native-value-detail.png` is captured by the native SQLite detail-view test on the second binary window. Text, UTF-8 boundaries, and disconnect behavior are checked separately by native and model tests.

The export state contract in [export.md](export.md) remains applicable except for its superseded nonmodal layout; the MVP spec requires modal export.

Saved-profile layout and behavior are specified in [profiles.md](profiles.md).

History and disconnected workspace restoration follow [history-recovery.md](history-recovery.md).

`native-recovery.png` shows a restored dirty Unicode buffer with no active connection; the native restart tests verify disconnected behavior and subsequent close-time persistence.

`native-history.png` records the populated history dock using a real two-row SQLite query. The native flow tests additionally verify reopening without execution, disabling/clearing history, saved-profile association, and close-time persistence.

Editor search and replacement follow [editor-search.md](editor-search.md). `native-search.png` shows the native panel after replacing two matches without a database connection.
