# Unified workspace status line

- Status: Ready for implementation
- Date: 2026-10-01
- Source: User's Object Data footer screenshot, repository inspection, and brainstorm decisions in this session.

## Outcome and current state

Unify the five workspace bottom bars through the existing `design::StatusLine`. A change to its shared style must update every workspace bar without screen-specific size, padding, typography, paging-control style, or overflow implementations.

### Complete workspace inventory

| Screen / variant | Construction and object names | Current presentation and data |
| --- | --- | --- |
| SQL Results and Messages | `desktop/app/main_window_ui.cpp`: `sqlResultFooter`, `executionSummary`, `executionStateCompact`, `executionDuration`, `executionPage`, `executionRows`, `executionVisibleSize`, `previousPage`, `nextPage` | Shared StatusLine with separate source, outcome and metric labels; Ghost IconSmall pagination. One bar below the result/messages stack. |
| Object Data, including tables/views and referenced-row navigation | `desktop/app/object_data_workspace.cpp`: `objectDataFooter`, `objectDataSummary`, `objectDataPrevious`, `objectDataNext` | Custom QWidget with one combined source/status/metrics label; Outline IconSmall pagination. Uses QueryWorkspace result lifecycle. |
| Object inspection: Columns/Details, Indexes, Keys, DDL, ERD | `desktop/app/object_explorer.cpp`: `objectFooter`, `objectStatus` | Shared StatusLine with a single status label. Data is another pane of this explorer. |
| Query History | `desktop/widgets/history_dock/history_dock.cpp`: `historyFooter`, `historyRange`, `historyStatus`, `historyPaging`, `historyPrevious`, `historyNext` | Shared StatusLine with row range and status; Outline IconSmall pagination in ButtonGroup. |
| Start / no database open | `desktop/app/main_window_ui.cpp`: `startFooter` | Custom subtle QWidget, one placeholder-colored label saying PostgreSQL · MySQL · SQLite. |

ObjectExplorer currently reparents ObjectDataWorkspace's custom footer into `objectFooter`, zeros the nested layout margins, and hides `objectStatus`. Avoid replacing this with two styled bars or doubled padding.

### Existing style ownership and affected code

- `desktop/design_system/status_line/status_line.{h,cpp}` owns an 8 px horizontal / 4 px vertical inset and 8 px item spacing through shared tokens, Ui typography (13 px), and success/danger surfaces controlled by `setAvailable(bool)`. Height is currently content-driven.
- Start instead uses 10 px horizontal padding. Data duplicates shared padding before ObjectExplorer removes it on embedding. History's status currently allows wrapping.
- `desktop/app/main_window_workspace.cpp` owns SQL-only `fitResultFooter`, source middle ellipsis, outcome right ellipsis, and an outcome width cap. Its metric allocation order is page, duration, rows, visible size.
- `desktop/app/main_window.cpp::refreshResultFooterColor` colors SQL by selected-target availability. ObjectExplorer colors by connection presence. History colors by `!failed_`. These are different meanings and must be replaced.
- `desktop/app/query_workspace_view.cpp::setExecutionState` builds a full summary and updates labels/tooltips/accessibility. `query_workspace.cpp` assigns result origins and wires pagination; `query_workspace_events.cpp` supplies outcomes, errors and metrics.
- Shared tokens are in `desktop/design_system/metrics/` and typography in `desktop/design_system/fonts/`. Reuse these foundations.
- Gallery: `desktop/tools/preview/preview_standard.cpp`, status-line registration in `preview_window.cpp`; ownership map in `desktop/design_system/README.md`.

### All current footer data and variants

SQL and Data share these fields:

| Field | Meaning / current variants |
| --- | --- |
| Source | SQL document title and target label, or object's qualified display name. Preserve origin of the displayed result. |
| Outcome | Disconnected, queued, running, cancelling, completed, cancelled, failed. |
| Duration | Backend query execution duration in milliseconds, retained across paging/filtering of the same execution. |
| Page | One-based displayed page index, when a result page applies. |
| Rows | Current page row count; affected rows when supplied by the backend; both can apply. |
| Visible memory | ResultTableModel resident bytes divided by 1024, rounded down; `%1 KiB visible`. This is visible-model memory, not a total result or database size. |
| View progress | Preparing result view, rows scanned, loading result view, matching rows. |
| Other outcomes | Loading object data/page, cancelling result view, previous result view restored, no rows match active filters with a clear-filters instruction. |
| Failures | Submission failed, object read submission failed, failed result page transfer/load, referenced-row filter failed, backend query error. |
| Initial/reset | SQL connect/run instruction and Data open/read instruction. |

Detailed backend query errors and vendor codes currently go to Messages while the footer says Failed. Warnings, transaction details and additional-result notices remain in their existing diagnostics surfaces. The Next button already changes tooltip/accessibility to Next result when another result set is available; preserve that behavior.

Object inspection data includes selection instructions, object label, metadata row counts or empty messages, DDL/Details loaded, restored-tab fresh-load instructions, connection-unavailable/reconnect instructions, unsupported/unavailable reasons, metadata/ERD errors, related-table and foreign-key counts, no relationships, incomplete ERD reasons/warnings, and busy navigation explanations. Loading text currently appears in progress toasts while the footer label is cleared. Data's busy navigation guard normally uses a warning toast rather than adding another footer.

History data includes loaded row range, No rows, After row N, No query history on this page, and backend operation errors. Loading, clearing and history-preference operations have existing progress toasts. Its row range describes the loaded page, not a global filtered total.

Excluded modal/action footers: DialogSections in Profile/Connection, Preferences, Export and Diagnostics; RightSheet in the row JSON viewer. Their shell contracts and layout remain outside this change. Gallery specimens are verification surfaces, not additional production screens.

## Decisions and requirements

### Shared component contract

1. Extend existing StatusLine rather than introducing a competing footer component. It owns layout, surfaces, text presentation, paging-control appearance, common height, insets, spacing, separators and overflow.
2. Screens provide structured presentation content, semantic outcome and action enabled state/callbacks. Do not recover data by parsing a combined display string. Keep the design system independent of application, database and bridge types.
3. All five use one common single-row height policy derived from shared typography/control/spacing tokens, including text-only bars. Centralize any required new dimension in metrics. No screen chooses a custom height, padding, font or paging style. Use one shared IconSmall chevron style (reuse Ghost as the compact existing option); grouping/presentation is also component-owned.
4. Remove Start/Data custom footer appearance and screen-owned fit/elision routines. Data can contribute content to the explorer bar or use the same component through a safe composition boundary, but the visible Data pane has exactly one styled surface and one set of insets.
5. Preserve existing object names, accessible names, focus behavior and action signals where their contracts still fit. Preserve keyboard pagination, cancellation/navigation guards, and Next result semantics. Export/edit/cancel actions retain their existing toolbar/menu locations.

### Content layout

| Screen | Leading content | Trailing content |
| --- | --- | --- |
| Start | Exactly one label centered across the entire bar: Choose a connection to get started | None |
| SQL / Object Data | Source · Outcome · Executed in N ms · N KiB visible | Page N · N rows, plus affected rows when applicable, immediately beside Previous/Next |
| Object inspection | Object/status/inspection explanation | None |
| History | Status or error explanation, when present | Existing row range immediately beside Previous/Next |

Duration uses `Executed in %1 ms`. Preserve backend duration semantics: paging/filtering does not create a new execution duration. Keep visible memory in the bar when space permits. Keep existing information and units, including affected rows. Omit inapplicable fields and separators; no empty slots or stale metrics after a new run, failed run, invalidation, document/pane switch, or result-type change. Start no longer lists supported database engines.

### Semantic colors and errors

Use one shared style with three semantic states, in both Light and Dark themes:

- Neutral gray for Start, initial/idle/no result, loading, queued, running, cancelling, user cancellation, and ordinary disconnected/no-target states.
- Green for successful execution or a successful content operation/load, including successful empty results. A connection becoming available alone is not success.
- Red for actual operation errors, including query submission/execution, result page, inspection, history-operation and connection failures reported as errors. Show the error explanation in the bar. A query error on a live connection must be red.
- Unsupported metadata and incomplete/partial ERD content remain gray with their explanation. Actual metadata/ERD load failure is red. Keep limitations/warnings distinct from successful complete content.

Use existing neutral/success/danger theme roles and accessible state text, not literal screen colors. Error classification remains owned by existing Rust/backend behavior; Qt maps supplied outcomes to presentation. A disconnect reported as an operation failure is an error; an idle disconnected document is neutral.

Display `Failed: <backend error>` or the existing specific error explanation rather than only Failed. Preserve vendor code information when supplied. Keep full diagnostics in Messages and existing other surfaces; footer display does not replace them. Display error text as plain text. Long/multiline error content may be normalized to a single display line, but retain the complete original detail in tooltip/accessibility and diagnostics.

Error state belongs to the relevant operation/view, not a global connection. Retain a failure until its relevant retry/new operation/reset; entering a new operation is neutral, successful recovery is green, and another failure is red. Switching documents/object panes restores their applicable outcome rather than borrowing an unrelated screen's color. Preserve a restored-result warning explanation after a failed filter/sort attempt; restoring the old view must not erase that attempt's error into a generic green Completed state. Keep existing progress/warning toasts and their readiness guards.

This explicitly supersedes the connection-based color requirements and metric grouping in `docs/specs/2026-09-28-sql-result-status-line.md`. Do not implement both color rules or reintroduce removed navigator connection status.

### Shared overflow policy

- Keep one row with no wrapping, horizontal scrolling or clipped pagination controls.
- Prioritize error/outcome text and pagination. Shorten source text with middle ellipsis and outcome/error with right ellipsis as needed.
- Keep page and row information next to pagination; hide visible memory first, then execution duration before sacrificing page/row context.
- At widths too small for all remaining text, shorten/compress text within available space while retaining the buttons and a readable outcome/error indication. The shared layout owns this fallback.
- Every hidden/elided field remains available in a complete current-summary tooltip and accessible description, including error detail. Restore omitted fields automatically when space returns. Start text is centered independently of absent leading/trailing slots.

## Acceptance criteria and public test seams

Use the existing real-widget/native-action seams; inspect public widget text, palette, geometry, accessibility and enabled state after actual operation readiness. Do not test private string assembly as a substitute for visible behavior.

| Criterion | Observable seam |
| --- | --- |
| All five production bars use shared appearance, common row height/insets/typography/paging style; Data has one visible styled bar | MainWindow, ObjectExplorer/ObjectDataWorkspace and HistoryDock widget trees; compare real component geometry and shared metrics. Extend existing modern UI, object explorer/data and history tests. Check construction sites in ui_consistency JSON. |
| Start is gray, with exactly one centered instruction | MainWindow startFooter real widget text, alignment and resolved palette. |
| SQL and Data display source/outcome, prefixed duration and visible KiB; page/rows sit beside paging controls | Execute real SQLite SELECT and non-result commands through existing QueryWorkspace/Run and Data flows; inspect named labels and geometry. Preserve affected-row and empty-result semantics. |
| Paging updates page, row and memory fields without relabeling page-fetch time as execution time; Next result still works | Existing result lifecycle/pagination native seams; inspect labels, action signals, button enabled state/tooltips. |
| Connection availability alone is neutral; successful operation green; actual error red with its message | Run successful and failing SQL through MainWindow actions, drive Data errors and inspection/history completion/error events using existing fixtures; inspect text/palette after readiness. Replace old tests asserting a failed connected query is green. |
| Unsupported/partial metadata is neutral with explanation; actual load failure red | Existing ObjectExplorer inspection/ERD fixture seams and named objectStatus widget. |
| New operations clear obsolete outcome metrics and return neutral; retries, tab/pane switches and recovery do not leak state | Existing modern_ui_workspace, object_data_workspace, result_view_workspace, object_explorer and history tests using public navigation/operation flows. |
| Narrow bars retain usable controls and readable status; memory disappears before duration; full content remains accessible | Resize real component and screens at wide/narrow widths, inspect geometry/visibility/tooltips/accessibility, click paging actions, then widen and verify restoration. Include long source and error strings. |
| Appearance updates correctly in Light and Dark, including neutral/success/error and text-only/paginated examples | Update real StatusLine gallery specimens in desktop/tools/preview and matching tests/desktop/preview_test.cpp assertions in both themes. |

## Constraints and risks

- Follow repository CLAUDE.md and desktop/design_system/CLAUDE.md. Rust owns domain rules, durable state, database behavior and backend errors; C++ owns frontend transient state and presentation. This work introduces no database, pagination, execution or durable-state policy changes.
- Existing public test seams include `tests/desktop/modern_ui_workspace_test.cpp`, `object_data_workspace_test.cpp`, `object_explorer_test.cpp`, `history_test.cpp`, `result_view_workspace_test.cpp`, `grid_edit_workspace_test.cpp`, and `preview_test.cpp`. Review tests depending on summary state/object names before changing wiring.
- Replace boolean availability styling with a typed presentation-state contract or equivalent small semantic interface; do not leave parallel style authority in MainWindow/ObjectExplorer/HistoryDock.
- Avoid unsafe callbacks while reparenting/replacing Data content or tearing down a parent window. Preserve existing activity sources, navigation guards and cancellation focus restoration.
- Full error display changes an existing observable behavior. Use plain text and single-line presentation with complete tooltip/accessible detail to handle long errors without expanding bar height.
- Update design-system README ownership description from availability surface to neutral/success/error status presentation. Update Light/Dark gallery examples and their matching tests.
- Run `python3 scripts/ci/ui_consistency.py` and inspect `--json`; run `python3 scripts/ci/ui_policy.py`, `python3 scripts/ci/qss_policy.py`, and `python3 scripts/ci/cpp_ownership.py`. Resolve introduced/touched findings and report remaining pre-existing failures explicitly. Extend construction census detection/tests if a new pattern is needed.
- Build and run relevant native CTest targets, and the full native suite when dependencies are present. Synchronize checks on actual operation readiness rather than a single event-loop turn.
- No persistence migration, external rollout or release publication is required. This session writes the spec only and does not implement or claim runtime verification.

Read this whole spec and inspect the current workspace before implementing.
