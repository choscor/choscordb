# Editable table and result grids

Status: Confirmed for implementation  
Date: 2026-09-20  
Source: User brainstorm on sidebar table navigation and staged data editing.

## Outcome

Clicking a table in the sidebar opens its inspector on Data every time. Users can edit cells, add rows, and mark rows for deletion directly in eligible Data and query result grids, then review generated SQL and parameter values and confirm one atomic Apply operation. PostgreSQL and SQLite are in scope.

## Current state

- `desktop/app/main_window.cpp` opens or reuses object inspector tabs. `desktop/app/object_explorer.cpp` starts new objects on Columns (pane 0); Data is pane 4. Existing tabs may retain another pane.
- `desktop/app/object_data_workspace.cpp` explicitly labels the Data grid read only and uses `QueryWorkspace` in object read only mode. `desktop/app/query_workspace.cpp` handles both object paging and SQL results.
- `desktop/models/result_table_model.h` is a page limited, read only model with scalar, binary, and deferred cells. The existing bridge has object reads and SQL execution, but no reviewed, parameterized edit batch interface. Metadata exposes key information through driver catalogs; implementation must verify its completeness for both drivers.
- The SQL workspace already has manual transaction state and a generic confirmation for mutating SQL. Grid Apply needs its own review and atomic execution path.

## Requirements and decisions

1. Every sidebar click on a **table**, including one whose inspector tab already exists, selects that table's Data pane. Other object kinds retain their current navigation behavior. A user selecting another inspector pane directly may stay there until the next sidebar table click.
2. Base table Data panes support staged insert, update, and delete. Views and other objects remain read only. Tables without a stable key allow inserts only.
3. An SQL result is editable only when it is a simple SELECT of one base table, the target and source columns are unambiguous, and all columns of a stable key are present. The key is a primary key, or a nonnullable unique key if no primary key exists. Computed columns remain read only. Eligible results offer add, edit, and delete; other results remain read only with a clear reason. Do not infer editability from column names alone.
4. Existing key columns are read only. Other directly mapped scalar cells can be edited inline. Provide an explicit Set NULL action distinct from empty string. Binary and deferred large values are read only for this release. Added rows start with omitted cells: untouched blank cells use database defaults, while explicit NULL or entered values are included in INSERT. Validate required values through the database and show errors without losing edits.
5. Users can mark multiple selected rows for deletion; deleted rows are visibly marked and can be restored before Apply. Pending inserts, updates, and deletes are visibly distinguished. Changes are local until Apply.
6. Apply opens a read only review showing the generated statements and each bound parameter value, including clear NULL and binary representations. Users can cancel review without losing changes. Confirming executes the complete batch atomically with parameter binding and correctly quoted identifiers. The review must never turn values into executable SQL text by interpolation.
7. Reject stale updates and deletes when the database row no longer matches the loaded original. A zero or multiple row match is a conflict. Roll back the entire batch on any conflict, permission failure, constraint violation, type error, disconnect, or other statement failure. Keep staged edits and show the actionable database error near Apply. Do not silently overwrite concurrent changes.
8. Grid Apply is disabled while that connection has an active manual transaction. Explain that the user must commit or roll back it first. The edit batch manages its own atomic transaction and commits on success.
9. Refreshing, paging, switching tables or query results, rerunning a query, closing a tab, or any other action that would replace a grid with pending changes prompts Apply, Discard, or Cancel. Apply follows the SQL review and proceeds with navigation only after success; Discard proceeds; Cancel stays put. A failed Apply leaves the current view and edits intact.
10. After success, clear staged state and reload the current page from the database. Inserted rows may appear on another page according to query order. Show a success summary with affected row counts. Preserve existing paging, export, copy, and deferred value behavior when no edits are staged.

## User flows and failure cases

- Sidebar table click → table tab selected or created → Data selected and loaded. Repeated sidebar click returns to Data.
- Edit a scalar cell, Set NULL, add a row, or mark selected rows for deletion → grid shows pending state and enables Apply and undo/discard affordances → Apply opens SQL and values review → Confirm executes and reloads.
- When a result cannot be mapped safely, editing controls are disabled and the reason is available in the UI. A keyless table can still add rows.
- Failed validation or conflict leaves all pending edits visible and unchanged. Users may correct them or discard them. A refresh that could erase them uses the navigation prompt.
- The one button request means one Apply entry point for all staged operations, followed by the agreed SQL confirmation; edits do not execute on each cell change.

## Acceptance criteria and public test seams

| Observable criterion | Highest practical test seam |
| --- | --- |
| Sidebar table click selects Data on new and reused inspector tabs; other object navigation remains intact. | Desktop main window interaction test using navigator selection and object tab/pane state. |
| Eligible base table grids stage scalar edits, explicit NULL, default based inserts, and reversible multirow deletes without writing before Apply. | Desktop widget interaction test plus driver backed read before confirmation. |
| Keyless tables allow insertion only; views, unmappable SELECTs, missing key columns, computed columns, binary, and deferred cells have the specified read only behavior and reasons. | Desktop grid interaction tests backed by SQLite fixture and PostgreSQL integration fixture where applicable. |
| Eligible single table keyed SELECT results support all three operations; row identity is checked against metadata rather than guessed. | Public query execution to result grid tests against representative SELECT, alias, join, expression, and key cases on both drivers. |
| SQL review lists statements and bound values; cancel makes no writes; confirm executes safely for quotes, NULL, and special values. | Desktop confirmation flow plus bridge or core API tests using real driver fixtures. |
| A successful mixed batch commits together, reloads the page, clears markers, and reports counts. Any failed statement or stale row rolls back all writes and retains pending edits. | Public batch API integration tests on SQLite and PostgreSQL; desktop result state test. |
| Pending edit navigation prompts Apply, Discard, Cancel at refresh, page change, result replacement, table switch, and tab close; failed Apply stays on the edited view. | Desktop main window and workspace interaction tests through visible controls. |
| Active manual transaction prevents grid Apply and explains why; ordinary query transaction behavior remains coherent. | Desktop workspace interaction test and driver backed transaction scenario. |

## Constraints and likely affected areas

- Preserve the current page size and memory limits in `ResultTableModel`; staged state needs an explicit bound or a safe refusal when that bound is reached. Do not silently drop changes across page replacement.
- Implement edit eligibility and key discovery in a driver aware backend or bridge path; the desktop must not build unsafe SQL from display text. Support both PostgreSQL and SQLite quoting, parameter binding, defaults, and transactional errors.
- Coordinate object data and SQL result lifecycles in `ObjectDataWorkspace`, `ObjectExplorer`, `QueryWorkspace`, `MainWindow`, the result model, bridge, core, and driver metadata or execution interfaces as needed. Existing worktree edits in these files may be unrelated; inspect and preserve them.
- No schema migration or persisted edit draft is required. No rollout flag was requested. Existing saved workspace recovery should continue to open read only until an eligible result is loaded and should not restore uncommitted edits.

## Non goals, risks, and assumptions

- Editing views, joins, aggregates, multi table results, binary values, and deferred large values is outside this release.
- Cross page edit retention is outside this release: navigation must resolve pending edits before replacing the page.
- Query eligibility is intentionally conservative. If a shape cannot be proven safe, show it read only. Exact supported simple SELECT syntax can follow the available parser or result provenance, but must satisfy the observable eligibility rule above.
- Optimistic conflict detection needs a stable comparison of the original row. If a driver cannot safely compare a particular value or row shape, disable update/delete with a reason instead of weakening conflict checks.
- The generated SQL preview is explanatory; the executed operation uses bound parameters and must correspond to the reviewed batch.

## Fresh session handoff

Read this entire spec, inspect the current workspace and any uncommitted changes, then invoke `$implement` with `docs/specs/2026-09-20-editable-table-results.md`. Use the acceptance criteria and public seams above for TDD, and preserve unrelated worktree changes.
