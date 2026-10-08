# Typed table cells and foreign-key navigation

Status: Confirmed for implementation

Date: 2026-09-27

Source: User brainstorm and cell-link image in this conversation

## Outcome and current state

People browsing Object Data or SQL Results can choose boolean and native enum values without typing them, and can follow a verified foreign key from a result cell to the referenced row in a separate table tab. The supplied image shows a small link icon at the right edge of the cell, separate from its value.

Both grids use `QueryWorkspace` and `ResultTableModel`. `desktop/design_system/table/table_style.cpp` already installs a result-table delegate, but it only paints rows. Editing currently uses the default text editor; the model parses boolean text and has a separate typed `setNull` path. `ResultColumn` carries a database type and optional nullability, not enum choices, source provenance, or FK mappings. Object Data already has a SQL-expression filter bar and an engine-backed Apply operation over the result. `MainWindow::openObjectTab` currently reuses tabs for the same object. Existing driver metadata can describe FKs for inspection, but the result grid has no structured FK mapping; query edit inspection can identify some simple source columns but imposes edit eligibility requirements that are too strict for read-only FK navigation. PostgreSQL and MySQL native enum choices are not currently exposed to the result editor.

## Requirements and decisions

1. Start with the shared result-cell presentation/behavior in `desktop/design_system/table/` and have both grids consume it. Extend the existing delegate contract rather than adding a duplicate table delegate or a screen-owned visual style. It may receive neutral cell metadata or model roles and emit a link activation; it must not own database lookups, query state, or tab navigation. Preserve existing row painting, object names, selection, focus, edit triggers, accessibility, and signals unless the new control requires a specific extension.
2. In either grid, when a cell is editable and its type is verified boolean or native PostgreSQL/MySQL enum, opening edit mode by the existing double-click or keyboard Edit trigger uses a non-free-text dropdown. Boolean choices are true and false. Enum choices are the complete schema-defined labels in database order. Include a distinct NULL choice only if the actual source column is nullable. Selecting NULL stages a typed null; it must never stage the string `"NULL"`. Existing Apply/Discard, SQL review, validation, and transaction rules remain in force. Read-only cells do not acquire an editor. If the complete enum list cannot be verified, keep the existing text editor and current editing behavior; do not invent choices from visible rows. SQLite `CHECK`-constrained text is outside enum scope.
3. A visible link icon appears at the right edge of a non-NULL cell only when its source column and an unambiguous **single-column** FK mapping to one target table and target column have been verified. This includes read-only SQL results whose provenance can be established independently of edit permission. Do not infer an FK from matching names or display text. No link appears for composite keys, ambiguous mappings, unknown provenance, NULL, deferred/unavailable values, or unsupported targets. The icon is the click target; clicking the value retains normal selection and editing. Give the link a named, keyboard-accessible action for the current cell and an informative tooltip/accessibility label identifying the target.
4. Activating the link opens a **separate** Object Explorer tab for the referenced table, even when that table is already open elsewhere. Select its Data pane, fill its existing SQL-expression filter with a correctly quoted, exact typed equality condition on the referenced target column, and apply it through the existing result-view operation. Use the cell's currently displayed value, including a staged edit. The filter must match across the complete target result, not only its first page. The new tab owns its filter and paging state; other tabs are unchanged. The filter must remain visible and clearable in the normal Object Data controls.
5. A target lookup that returns no row shows the ordinary zero-match filtered state, including for a staged value that is not yet present. If target metadata, opening, or filter application fails, retain the new tab with a clear error and no misleading unfiltered rows. Do not silently fall back to an unfiltered table. Stale asynchronous metadata/filter completions must not alter a different result or tab. Existing pending-edit and active-work guards remain authoritative when navigating.
6. Keep this feature transient. No saved filter, migration, generated SQL editor tab, composite-FK navigation, SQLite enum inference, or broader query-provenance guessing is required. Preserve existing supported drivers and database behavior where metadata is unavailable by retaining ordinary cells and current editing.

## User flows

- Double-click or keyboard-edit an eligible boolean/enum cell. The dropdown reflects the current value, offers allowed values and optional NULL, and stages only the chosen typed value. Escape/cancel leaves the cell unchanged. Apply and Discard continue through the current review path.
- In Object Data or a verified SQL result, see a right-aligned link icon beside a non-NULL FK value. Click the icon, or use its keyboard action, to open a new table tab. Its Data pane loads, the target-key equality filter is shown and applied, and the matching row is visible through normal paging. A click on the value itself continues to select/edit normally.
- A missing referenced row yields an empty filtered result. Metadata or filter failure yields an error in the new tab without unfiltered data.

## Acceptance criteria and public test seams

| ID | Observable outcome | Public seam / evidence |
| --- | --- | --- |
| AC1 | Both grids retain existing row appearance/interaction and show the same typed cell/link presentation where metadata qualifies. The shared component has visible Light and Dark specimens. | Native `QTableView` interactions in SQL Results and Object Data; design gallery and `tests/desktop/preview_test.cpp` selection of real controls in both themes. |
| AC2 | Editable booleans and native PostgreSQL/MySQL enums use dropdowns in existing edit mode; choices are exact and ordered, NULL is offered only for nullable columns, and cancellation makes no edit. | Public grid edit triggers plus model pending-edit state and Apply/Discard against disposable typed tables. Driver metadata tests cover ordered enum labels and nullability. |
| AC3 | Unavailable enum choices retain text editing, while read-only/unsupported columns do not gain an editor. | Inject unavailable metadata at the adapter/driver boundary; observe grid editor and unchanged write eligibility. |
| AC4 | Only verified, non-NULL, unambiguous single-column FK cells show the right-edge link; read-only SQL results can show it without edit eligibility. Value clicks retain normal behavior; icon and keyboard action navigate. | Driver/adapter metadata fixtures for single, composite, ambiguous, NULL and simple read-only query cases; native hit-target, keyboard, tooltip and accessibility observations. |
| AC5 | Each activation creates a separate target table tab, selects Data, shows an exact equality SQL filter for the displayed typed value, applies it across pages, and leaves other tabs unchanged. | Native tab/filter controls with SQLite, PostgreSQL and MySQL disposable FK fixtures where available; verify target rows beyond page one and staged-source-value behavior. |
| AC6 | Zero matches stay filtered; open/filter failures never display unfiltered target rows; stale completions cannot replace another tab's result. | Public adapter event seam with delayed/failing metadata or filter response and native target-tab status, controls, and rows. |

## Technical constraints and likely affected areas

Read `desktop/design_system/README.md` and `desktop/design_system/CLAUDE.md` before modifying the shared table delegate. The existing `configureResultTable` path is the integration point; use shared tokens, icon roles, QSS and Qt stock-control styling. Update its Light/Dark gallery specimen and matching preview test; update the component map if introducing a new family. Keep per-result eligibility, metadata request tokens, FK action handling, and navigation in `desktop/app/` and `desktop/models/`, with data retrieval in the bridge/driver layer. Likely areas: `desktop/design_system/table/`, `desktop/models/result_table_model.*`, `desktop/app/query_workspace*`, `desktop/app/object_data_workspace.*`, `desktop/app/main_window.*`, `desktop/app/result_filter_bar.*`, bridge DTOs, driver metadata, and focused desktop/driver tests.

Obtain structured enum and FK metadata from the database catalog; do not parse human-readable inspection strings or trust result type names alone for navigation. Resolve SQL-result provenance conservatively, separately from edit eligibility, and hide controls that cannot be proven. Preserve typed values and use safe quoting/escaping for generated filter expressions, including unusual identifiers and string values. Apply resource limits and stale-result protection consistent with the existing engine. If metadata acquisition fails, ordinary cells remain usable and the failure must not make an ineligible action appear.

Run `python3 scripts/ci/ui_consistency.py`, `python3 scripts/ci/ui_policy.py`, and `python3 scripts/ci/qss_policy.py`. Build and run the relevant native CTest targets, then the full native suite when dependencies are present. The canonical quality runner includes the consistency gate and Python tests.

## Risks and safe implementation assumptions

PostgreSQL domains or arrays over enums and MySQL `SET` are outside the native-enum scope unless their exact single-choice semantics can be established without guessing. A boolean-like MySQL integer should become a boolean dropdown only with authoritative boolean semantics; otherwise retain text editing. SQL expressions, joins, aliases, and duplicate source names without verified one-to-one provenance have no FK link. Use the existing pending-edit resolution and active-work behavior; do not discard source edits merely to navigate. Failure feedback may use the destination's existing status/diagnostics surface. No persistence migration or rollout flag is needed.

## Fresh-session instruction

Read this entire spec, inspect the current workspace and applicable repository instructions, then invoke `$implement` with this spec path. Recheck code and metadata contracts before choosing the implementation.

```text
Use $implement with docs/specs/2026-09-27-typed-table-cells-fk-navigation.md.
```
