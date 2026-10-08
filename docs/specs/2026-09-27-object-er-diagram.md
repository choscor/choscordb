# Object ER diagram pane

Status: Confirmed for implementation
Date: 2026-09-27
Source: User's request to add ERD between DDL and Data, followed by the 2026-09-27 brainstorm decisions.

## Outcome and current state

Help a database user understand a table's immediate foreign-key neighborhood without leaving its object tab. Add an **ERD** inner pane in the order **Columns → Indexes → Keys → DDL → ERD → Data**. Selecting ERD displays a readable drawing of the selected table and directly related tables.

`desktop/app/object_explorer.cpp` currently constructs five inner panes and uses numeric pane indices for requests, visibility, Data controls, and state. `desktop/app/workspace_recovery.cpp` saves the selected pane, and `crates/storage/src/lib.rs` validates values through 4. The existing `EngineAdapter::loadObjectInspection` returns per-object columns and keys; key properties are partly display strings. There is no structured relationship graph interface or ERD component. `docs/specs/2026-09-19-unified-workspace-tabs.md` establishes lazy object loading, inert restore, per-tab request isolation, and manual reconnect.

## Requirements and decisions

1. Show ERD for **table objects only**. Keep it hidden for views, indexes, sequences, functions, and other object kinds. Preserve the existing applicable pane choices for those kinds.
2. The graph contains the selected table plus every table with a direct incoming or outgoing declared foreign key, including reachable tables in another schema of the same connection. Do not expand beyond one relationship hop. A self-reference is one box with a loop. Distinct constraints remain distinct, including parallel and composite foreign keys. Use stable qualified object identities, not display labels, to join and open tables.
3. Each table box shows its qualified name, all column names and database types, and visible PK/FK markers. Draw each FK between the corresponding source and target columns, with clear source-to-target direction. Show cardinality/optionality only where structured metadata proves it; otherwise use an unlabeled directional FK connector. Do not infer relationships by name or parse formatted DDL/Keys text. If a target column cannot be determined, identify the constraint and its target table without fabricating a column endpoint.
4. Generate a readable initial layout centered on the selected table, with an initial fit to the available viewport. Support pointer pan and zoom plus keyboard-accessible equivalent controls (at least zoom in, zoom out, fit/reset). Activating a related table by pointer or keyboard opens/focuses its existing object tab through the normal navigation route. The drawing is read-only; no schema edits or SQL execution occur.
5. Load the graph only when ERD is selected or explicitly refreshed. Merely refocusing an already loaded object tab does not read again. Refresh rebuilds from current metadata. Match replies to the object, connection, and newest request so stale or disconnected results cannot replace the current graph. Reuse the existing busy-operation navigation guard so ERD cannot hide an active Data Cancel control.
6. Show the selected table alone with an explicit “No foreign-key relationships” status when none exist. Show loading, retryable failure, unsupported metadata, and disconnected/manual-reconnect states through the established object status surfaces. If metadata limits, permissions, or an unavailable related table prevent a complete graph, state that the graph is incomplete and identify the affected relationship/table; never silently omit a known edge or present partial data as complete. Bound graph reads and rendering in line with existing metadata limits; remain responsive on large schemas.
7. Save and restore ERD as a selected inner pane without saving graph contents, live handles, or viewport position. Old saved pane values 0–3 retain their meaning and old value 4 still restores **Data**. The new ERD value needs an explicit, validated mapping in both desktop and Rust recovery. Restored ERD tabs remain inert until activated and connected, as in the existing recovery contract.
8. Support the project's currently available SQLite, PostgreSQL, and MySQL connections where the driver can return trustworthy structured FK and column metadata. Report unsupported capability explicitly rather than deriving relationships from SQL text. Use the current design-system tokens, shared controls, Light/Dark themes, accessibility roles, and ownership rules for graph presentation.

The selected-table neighborhood was chosen over a whole-schema graph to keep the pane focused. The table boxes include all columns so relationship endpoints and the rest of the table are understandable together. Navigation uses existing object tabs. Export, persisted manual layout, drag-to-edit, relationship creation, and a schema-wide diagram are outside this feature.

## User-visible flow and edge cases

- Open a table object tab and choose ERD. The tab sits between DDL and Data; the viewport loads the selected table and both inbound and outbound one-hop relationships, then fits the graph. The central table is visually distinguishable. Pan or zoom to inspect it; activate another table to open/focus that table's object tab.
- A table without FKs displays its columns and a clear no-relationships state. A self-FK or multiple FKs to one neighbor draws each constraint without duplicating table boxes. A composite FK keeps its paired column mapping and order.
- Refresh requests fresh data. A failed or incomplete refresh does not leave an old drawing labeled as current. Retry is available for transient failures. Disconnect clears the live graph and offers manual reconnect. Restored tabs do not fetch until selected and connected.
- A relationship to a missing or inaccessible table is reported as incomplete. Loading limits or unavailable FK metadata also produce explicit status. Do not display guessed columns or cardinality.
- Standard table/view inspection, DDL, Data paging and edit guards, object-tab identity, and old workspace recovery remain intact.

## Acceptance criteria and public test seams

| Observable outcome | Public test seam |
| --- | --- |
| A table has ERD between DDL and Data; other object kinds do not. Existing Data selection and actions still work. | Open each kind through `ObjectExplorer`/main-window public navigation; inspect visible `objectTabs`, selected pane, and Data controls. |
| A known SQLite/Pg/MySQL fixture draws all columns, types, PK/FK markers, direct inbound/outbound tables, and correct paired FK endpoints; no second-hop table appears. | Drive the public adapter/ERD loading boundary against representative database fixtures and inspect a public graph model or accessible scene items. Include composite, self, cross-schema (where supported), and parallel FKs. |
| FK direction is correct and unsupported cardinality is not invented. | Inspect semantic relationship data exposed by the scene/accessibility layer from database fixtures with known constraints. |
| ERD supports fit, pointer pan/zoom, keyboard zoom and table activation. | Qt widget/integration tests using the public controls and input events; observe viewport transform and top-level object-tab identity. Verify Light/Dark captures at normal and narrow widths. |
| No-FK, loading, incomplete, unsupported, failed/retry, disconnect, and reconnect states are explicit. | Control the adapter's public metadata responses and connection events; observe `ObjectExplorer` status, retry/reconnect actions, and scene contents. |
| Lazy load, refresh, stale reply rejection, and busy Data navigation guard work. | Use adapter request/response signals with delayed replies and public pane/tab actions; count reads and inspect the active pane/Cancel control. |
| New ERD selection survives restart without persisting graph data; old saved value 4 still selects Data. | Save/restore at the public workspace recovery/bridge boundary using new and legacy fixtures; inspect selected pane and unexpected adapter reads. |
| Design presentation passes repository UI rules. | Run `scripts/ci/ui_consistency.py`, `scripts/ci/ui_policy.py`, `scripts/ci/qss_policy.py`; run the Light/Dark gallery specimen and test if a design-system component is added or changed. |

## Technical constraints and affected areas

Likely work spans `desktop/app/object_explorer.*`, a graph widget/model in `desktop/app/` or `desktop/widgets/`, `desktop/bridge/engine_adapter.*`, structured metadata in the Rust drivers/bridge, workspace recovery and storage validation, desktop/native tests, and possibly `desktop/design_system/`. Keep database/workflow state outside the design system and reusable appearance inside it. Inspect `desktop/design_system/README.md` ownership before extraction; follow `desktop/design_system/CLAUDE.md` for any design-system edit, including the Light/Dark gallery specimen and test. Preserve object names, accessibility names, focus, and signals where possible. Use structured, bounded metadata and stable qualified identities. A rendering mechanism such as Qt Graphics View is an implementation choice, not a requirement.

Normal desktop release is sufficient; no data migration beyond backward-compatible recovery decoding is intended. The current pane-index validation and all tests assuming Data index 4 must be updated without changing legacy records' meaning. Run the repository UI checks and relevant native CTest targets, then the full native suite when dependencies are present.

## Risks and deferred choices

Incoming-FK discovery may require bounded catalog queries beyond today's per-table Keys response; permissions and dialect differences need explicit incomplete/unsupported outcomes. Existing display-oriented FK properties are insufficient as a canonical graph API. Very wide or dense tables need a deterministic legible layout and responsive interaction. Exact graph transport shape, quantitative safety limits, keyboard bindings, layout algorithm, and transient viewport retention while switching panes are implementation choices, provided the observable outcomes above hold. No persistent viewport state is required.

## Fresh-session instruction

Read this whole spec, inspect the current workspace and relevant tests, then invoke `$implement` with `docs/specs/2026-09-27-object-er-diagram.md`.
