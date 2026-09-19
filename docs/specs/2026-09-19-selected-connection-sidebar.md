# Selected connection sidebar and grouped objects

Status: Confirmed for implementation
Date: 2026-09-19
Source: User request with sidebar screenshot and brainstorm decisions confirmed on 2026-09-19.

## Outcome

Make Schema & Objects a predictable explorer for the highlighted saved connection. Organize supported object types under each schema, expose the requested PostgreSQL and SQLite metadata, and show a useful modal when a sidebar-initiated connection open fails.

## Current state and workspace findings

- `desktop/app/main_window.cpp` contains the saved profile list, navigator tree, and footer status. Clicking a saved profile can open a session, but the tree currently holds roots for every connected session. A `connection_failed` event updates the footer; a profile submission error uses a notice.
- `desktop/app/navigator_controller.cpp` connects `NavigatorModel` to lazy metadata requests. `desktop/models/navigator_model.cpp` stores all session roots and provides completion snapshots.
- `crates/driver-api/src/driver.rs` has object kinds for database, schema, table, view, column, keys, and index. It has no sequence or function kind. Current PostgreSQL schema metadata returns relations; SQLite database metadata returns tables and views. Both expose indexes under tables.
- `desktop/app/object_explorer.cpp` and the bridge provide existing inspection and DDL paths, primarily for current object types. Existing desktop tests include `tests/desktop/navigator_model_test.cpp`, `tests/desktop/navigator_sql_workspace_test.cpp`, and `tests/desktop/modern_ui_test.cpp`; driver tests live under `crates/driver-postgres` and `crates/driver-sqlite`.
- The working tree already had unrelated changes when this spec was written, including `desktop/app/main_window.cpp` and `tests/desktop/modern_ui_test.cpp`. Inspect current files before implementation and preserve those changes.

## Requirements and decisions

1. The highlighted saved profile in the sidebar determines the sole connection shown in Schema & Objects. The SQL editor's target may differ. Switching the highlighted profile switches the visible tree without changing the editor target.
2. The hierarchy is connection → database/schema → supported type groups → objects. Use plural group labels such as Tables, Views, Indexes, Sequences, and Functions. Show a supported group even when it has no objects. Do not show groups that the driver cannot browse. Derive groups from driver metadata/capabilities so future drivers can add types without a fixed UI-only list.
3. Add PostgreSQL discovery for schema-level indexes, sequences, and functions, and SQLite discovery for schema-level indexes. Keep indexes under their parent tables as well. Identity and qualified names must remain unambiguous for duplicate index paths and overloaded functions.
4. Loading remains lazy: opening a schema reveals supported groups; expanding a group loads its objects. Large catalogs must respect existing metadata limits and request-token behavior. A group load failure must show a retryable error in that group without replacing other groups.
5. The filter searches object names across the selected connection, including collapsed groups. Search is bounded and cancellable/stale-result safe; if results are truncated, tell the user to refine the text. Search results retain enough ancestor context to identify the schema and group. Clearing the filter restores the ordinary lazy tree state. Search must never include another connection.
6. Selecting a sequence, function, or schema-level index opens basic object details where the driver supports them: name, kind, schema, available metadata, and DDL when available. An unsupported field or DDL state is explicit rather than silently blank. Copy qualified name and other applicable context actions remain available. SQL generation remains limited to tables and views.
7. When a sidebar list click or its Connect action starts opening a profile, highlight it immediately and show a loading tree. On success, show only its objects and remember it as the last successfully browsed profile. On failure, show one modal with the profile name and actual database or submission error, then restore the last successfully browsed profile. If that profile has disconnected or been deleted, clear the selection and tree; do not reconnect automatically. Preserve footer status as a secondary signal. An error from an SQL workspace-initiated open keeps its existing handling.
8. A stale response from a superseded connection attempt, metadata load, or search must not change the currently highlighted profile's tree or produce a misleading modal. Disconnecting or deleting the highlighted profile clears its tree unless a valid previously browsed profile is explicitly selected.

## Non-goals

- Changing the SQL editor's connection target when the sidebar selection changes.
- SQL generation or data browsing for sequences, functions, or indexes.
- Discovery of object types beyond those named above for this release.
- Changing error presentation for connections opened outside the sidebar.

## User-visible flows and edge cases

- With two connected saved profiles, selecting either one shows only its database/schema objects. Returning to a previously browsed profile may reuse valid loaded metadata; refresh requests current metadata.
- A schema with no views still displays Views as an empty supported group. SQLite does not display PostgreSQL-only groups. A future driver supplies its own supported groups.
- Searching a collapsed schema finds matching objects in that selected connection, with a visible loading state and a refine message when bounded. Rapidly changing the text or selection discards stale results.
- Both paths to an index identify the same database object. PostgreSQL functions with the same name remain individually identifiable by signature or another unambiguous label.
- A failed sidebar open presents a readable modal containing the profile name and returned reason. Dismissal leaves the last valid profile highlighted and its tree visible, or leaves no selection if none is available.

## Acceptance criteria and public test seams

| Observable criterion | Preferred test seam |
| --- | --- |
| Highlighting profile A shows only A's objects, switching to B shows only B's, and the SQL editor target stays unchanged. | `MainWindow` with saved profiles and a `QTreeView`/proxy, exercising list selection; assert visible tree and editor target. |
| Schema groups reflect driver support, including empty supported groups; objects load on group expansion. | Driver metadata API integration tests plus `NavigatorController`/tree interaction tests. |
| PostgreSQL exposes indexes, sequences, and distinguishable overloaded functions; SQLite exposes schema indexes; indexes remain under tables. | Driver metadata integration tests using temporary databases and public `load_metadata` results. |
| New object types open basic detail views with accurate available/unsupported metadata and DDL behavior; table/view SQL generation is unchanged. | Bridge inspection/DDL requests and `ObjectExplorer` UI tests. |
| Filtering reaches collapsed groups for the selected profile, stays bounded, shows a refine message, and ignores stale replies. | Filter widget through navigator controller and metadata request/reply seam; include rapid input and profile switch cases. |
| Sidebar open failure shows one modal with reason and restores the last valid selection, or clears it if unavailable; unrelated open failures do not show this modal. | `MainWindow` with adapter event/submission seams, inspecting modal text and sidebar state. |
| A late metadata or connection response cannot repopulate another selected profile's tree. | Public adapter event to navigator/controller/UI integration test using reversed reply order. |

## Technical constraints and likely affected areas

- Preserve lazy loading, bounded metadata, token checks, and completion behavior. Group nodes are presentation/metadata structure; do not let them become invalid SQL identifiers or completion candidates.
- Extend the driver API and bridge representation as needed for sequence/function kinds, supported groups, inspection, and search. The exact representation is an implementation choice, but it must be driver extensible and avoid fetching every object merely to display empty groups.
- Update `desktop/app/main_window.cpp`, navigator controller/model, object explorer, relevant bridge code, and PostgreSQL/SQLite metadata as needed. Maintain keyboard access, accessible names, and existing context actions.
- Avoid exposing credentials in the modal or logs; display the returned diagnostic reason. Handle long reasons in a readable dialog.
- No data migration is expected. Existing saved profiles and open sessions remain usable. Existing desktop and driver test suites are the rollout gate.

## Risks and deferred implementation choices

- Searching all schemas can be expensive. Reuse existing catalog limits where practical, cancel or ignore superseded work, and make truncation visible. The precise cap and search API are implementation choices.
- Some drivers may lack DDL or rich properties for a type. Show explicit unavailable/unsupported states. The exact detail layout is deferred.
- The user confirmed behavior, not a particular internal model design or modal class. Choose the simplest design consistent with the public seams above.

## Fresh-session instruction

Read this entire spec and inspect the current workspace, including pre-existing changes. Then invoke `$implement` with `docs/specs/2026-09-19-selected-connection-sidebar.md` and implement the confirmed behavior with tests at the stated public seams.
